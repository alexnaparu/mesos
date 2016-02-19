// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include <list>
#include <string>
#include <vector>

#include "executor.hpp"

using namespace mesos::internal::slave;

using process::wait; // Necessary on some OS's to disambiguate.

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;

namespace mesos {
namespace internal {

using namespace process;


void CommandExecutorProcess::_launchTask(
    const string& command,
    const char** argv)
{
  // TODO(benh): Clean this up with the new 'Fork' abstraction.
  // Use pipes to determine which child has successfully changed
  // session. This is needed as the setsid call can fail from other
  // processes having the same group id.
  process::Pipe pipe;
  Try<Nothing> createPipe = pipe.Create();

  if (createPipe.isError()) {
    perror("Failed to create IPC pipe");
    abort();
  }

  // Set the FD_CLOEXEC flags on these pipes.
  Try<Nothing> cloexec = os::cloexec(pipe.read);
  if (cloexec.isError()) {
    cerr << "Failed to cloexec(pipe[0]): " << cloexec.error() << endl;
    abort();
  }

  cloexec = os::cloexec(pipe.write);
  if (cloexec.isError()) {
    cerr << "Failed to cloexec(pipe[1]): " << cloexec.error() << endl;
    abort();
  }

  Option<string> rootfs;
  if (sandboxDirectory.isSome()) {
    // If 'sandbox_diretory' is specified, that means the user
    // task specifies a root filesystem, and that root filesystem has
    // already been prepared at COMMAND_EXECUTOR_ROOTFS_CONTAINER_PATH.
    // The command executor is responsible for mounting the sandbox
    // into the root filesystem, chrooting into it and changing the
    // user before exec-ing the user process.
    //
    // TODO(gilbert): Consider a better way to detect if a root
    // filesystem is specified for the command task.
#ifdef __linux__
    Result<string> user = os::user();
    if (user.isError()) {
      cerr << "Failed to get current user: " << user.error() << endl;
      abort();
    }
    else if (user.isNone()) {
      cerr << "Current username is not found" << endl;
      abort();
    }
    else if (user.get() != "root") {
      cerr << "The command executor requires root with rootfs" << endl;
      abort();
    }

    rootfs = path::join(
      os::getcwd(), COMMAND_EXECUTOR_ROOTFS_CONTAINER_PATH);

    string sandbox = path::join(rootfs.get(), sandboxDirectory.get());
    if (!os::exists(sandbox)) {
      Try<Nothing> mkdir = os::mkdir(sandbox);
      if (mkdir.isError()) {
        cerr << "Failed to create sandbox mount point  at '"
          << sandbox << "': " << mkdir.error() << endl;
        abort();
      }
    }

    // Mount the sandbox into the container rootfs.
    // We need to perform a recursive mount because we want all the
    // volume mounts in the sandbox to be also mounted in the container
    // root filesystem. However, since the container root filesystem
    // is also mounted in the sandbox, after the recursive mount we
    // also need to unmount the root filesystem in the mounted sandbox.
    Try<Nothing> mount = fs::mount(
      os::getcwd(),
      sandbox,
      None(),
      MS_BIND | MS_REC,
      NULL);

    if (mount.isError()) {
      cerr << "Unable to mount the work directory into container "
        << "rootfs: " << mount.error() << endl;;
      abort();
    }

    // Umount the root filesystem path in the mounted sandbox after
    // the recursive mount.
    Try<Nothing> unmountAll = fs::unmountAll(path::join(
      sandbox,
      COMMAND_EXECUTOR_ROOTFS_CONTAINER_PATH));
    if (unmountAll.isError()) {
      cerr << "Unable to unmount rootfs under mounted sandbox: "
        << unmountAll.error() << endl;
      abort();
    }
#else
    cerr << "Not expecting root volume with non-linux platform." << endl;
    abort();
#endif // __linux__
  }

  if ((pid = fork()) == -1) {
    cerr << "Failed to fork to run " << commandString << ": "
      << os::strerror(errno) << endl;
    abort();
  }

  // TODO(jieyu): Make the child process async signal safe.
  if (pid == 0) {
    // In child process, we make cleanup easier by putting process
    // into it's own session.
    os::close(pipe.read);

    // NOTE: We setsid() in a loop because setsid() might fail if another
    // process has the same process group id as the calling process.
    while ((pid = setsid()) == -1) {
      perror("Could not put command in its own session, setsid");

      cout << "Forking another process and retrying" << endl;

      if ((pid = fork()) == -1) {
        perror("Failed to fork to launch command");
        abort();
      }

      if (pid > 0) {
        // In parent process. It is ok to suicide here, because
        // we're not watching this process.
        exit(0);
      }
    }

    if (write(pipe.write, &pid, sizeof(pid)) != sizeof(pid)) {
      perror("Failed to write PID on pipe");
      abort();
    }

    os::close(pipe.write);

    if (rootfs.isSome()) {
#ifdef __linux__
      if (user.isSome()) {
        // This is a work around to fix the problem that after we chroot
        // os::su call afterwards failed because the linker may not be
        // able to find the necessary library in the rootfs.
        // We call os::su before chroot here to force the linker to load
        // into memory.
        // We also assume it's safe to su to "root" user since
        // filesystem/linux.cpp checks for root already.
        os::su("root");
      }

      Try<Nothing> chroot = fs::chroot::enter(rootfs.get());
      if (chroot.isError()) {
        cerr << "Failed to enter chroot '" << rootfs.get()
          << "': " << chroot.error() << endl;;
        abort();
      }

      // Determine the current working directory for the executor.
      string cwd;
      if (workingDirectory.isSome()) {
        cwd = workingDirectory.get();
      }
      else {
        CHECK_SOME(sandboxDirectory);
        cwd = sandboxDirectory.get();
      }

      Try<Nothing> chdir = os::chdir(cwd);
      if (chdir.isError()) {
        cerr << "Failed to chdir into current working directory '"
          << cwd << "': " << chdir.error() << endl;
        abort();
      }

      if (user.isSome()) {
        Try<Nothing> su = os::su(user.get());
        if (su.isError()) {
          cerr << "Failed to change user to '" << user.get() << "': "
            << su.error() << endl;
          abort();
        }
      }
#else
      cerr << "Rootfs is only supported on Linux" << endl;
      abort();
#endif // __linux__
    }

    // The child has successfully setsid, now run the command.
    if (override.isNone()) {
      if (command.shell()) {
        execlp(
          os::Shell::name,
          os::Shell::arg0,
          os::Shell::arg1,
          task.command().value().c_str(),
          (char*)NULL);
      }
      else {
        execvp(command.value().c_str(), argv);
      }
    }
    else {
      char** argv = override.get();
      execvp(argv[0], argv);
    }

    perror("Failed to exec");
    abort();
  }

  // In parent process.
  os::close(pipe.write);

  // Get the child's pid via the pipe.
  if (read(pipe.read, &pid, sizeof(pid)) == -1) {
    cerr << "Failed to get child PID from pipe, read: "
      << os::strerror(errno) << endl;
    abort();
  }

  os::close(pipe.read);
}

} // namespace internal {
} // namespace mesos {
