// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <string>

#include <glog/logging.h>

#include <process/future.hpp>
#include <process/reap.hpp>
#include <process/subprocess.hpp>

#include <stout/error.hpp>
#include <stout/lambda.hpp>
#include <stout/foreach.hpp>
#include <stout/option.hpp>
#include <stout/os.hpp>
#include <stout/os/strerror.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>
#include <stout/unreachable.hpp>
#include <stout/windows.hpp>

using std::map;
using std::string;
using std::vector;

namespace process {

using InputFileDescriptors = Subprocess::IO::InputFileDescriptors;
using OutputFileDescriptors = Subprocess::IO::OutputFileDescriptors;

namespace internal {

void cleanup(
    const Future<Option<int>>& result,
    Promise<Option<int>>* promise,
    const Subprocess& subprocess);

static void close(
  const InputFileDescriptors& stdinfds,
  const OutputFileDescriptors& stdoutfds,
  const OutputFileDescriptors& stderrfds)
{
  HANDLE handles[6] = {
    stdinfds.read, stdinfds.write.getOrElse(INVALID_HANDLE_VALUE),
    stdoutfds.read.getOrElse(INVALID_HANDLE_VALUE), stdoutfds.write,
    stderrfds.read.getOrElse(INVALID_HANDLE_VALUE), stderrfds.write
};

  foreach(HANDLE handle, handles) {
    if (handle != INVALID_HANDLE_VALUE) {
      ::CloseHandle(handle);
    }
  }
}

// Opens a in inheritable pipe[1]. On success, `handles[0]` receives the 'read'
// handle of the pipe, while `handles[1]` receives the 'write' handle. The pipe
// handles can then be passed to a child process, as exemplified in [2].
//
// [1] https://msdn.microsoft.com/en-us/library/windows/desktop/aa379560(v=vs.85).aspx
// [2] https://msdn.microsoft.com/en-us/library/windows/desktop/ms682499(v=vs.85).aspx
Try<Nothing> CreatePipeHandles(HANDLE handles[2])
{
  SECURITY_ATTRIBUTES securityAttr;
  securityAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  securityAttr.bInheritHandle = TRUE;
  securityAttr.lpSecurityDescriptor = NULL;

  if (!::CreatePipe(&handles[0], &handles[1], &securityAttr, 0)) {
    return WindowsError("CreatePipeHandles: could not create pipe");
  }

  return Nothing();
}

// Launches a child process without waiting for its return. The `path` argument
// contains the full or relative path of the executable module to load.
// Environment variables can be passed to `environment` as a NULL-terminated
// array of NULL-terminated strings in a 'name=value\0' format. Quotes should
// be used wherever `value` contains a white space.
Try<pid_t> CreateChildProcess(
  const string& path,
  const vector<string>& argv,
  LPVOID environment,
  InputFileDescriptors stdinFds,
  OutputFileDescriptors stdoutFds,
  OutputFileDescriptors stderrFds)
{
  PROCESS_INFORMATION processInfo;
  STARTUPINFO startupInfo;

  ::ZeroMemory(&processInfo, sizeof(PROCESS_INFORMATION));
  ::ZeroMemory(&startupInfo, sizeof(STARTUPINFO));

  // Hook up the `stdin`/`stdout`/`stderr` pipes and use the
  // `STARTF_USESTDHANDLES` flag to instruct the child to use them[1]. A more
  // user-friendly example can be found in [2].
  //
  // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/ms686331(v=vs.85).aspx
  // [2] https://msdn.microsoft.com/en-us/library/windows/desktop/ms682499(v=vs.85).aspx
  startupInfo.cb = sizeof(STARTUPINFO);
  startupInfo.hStdError = stderrFds.write;
  startupInfo.hStdOutput = stdoutFds.write;
  startupInfo.hStdInput = stdinFds.read;
  startupInfo.dwFlags |= STARTF_USESTDHANDLES;

  // Build child process arguments (as a NULL-terminated string).
  size_t argLength = 0;
  foreach(string arg, argv) {
    argLength += arg.size() + 1;  // extra char for either ' ' or trailing NULL.
  }

  char *arguments = new char[argLength];
  size_t index = 0;
  foreach(string arg, argv) {
    strncpy(arguments + index, arg.c_str(), arg.size());
    index += arg.size();
    arguments[index] = ' ';
  }

  // NULL-terminate the arguments string.
  arguments[index] = '\0';

  // See the `CreateProcess` MSDN page[1] for details on how `path` and
  // `args` work together in this case.
  //
  // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/ms682425(v=vs.85).aspx
  const BOOL createProcessResult = CreateProcess(
      (LPSTR)path.c_str(),  // Path of module to load[1].
      (LPSTR)arguments,     // Command line arguments[1].
      NULL,                 // Default security attributes.
      NULL,                 // Default primary thread security attributes.
      TRUE,                 // Inherited parent process handles.
      0,                    // Default creation flags.
      environment,          // Array of environment variables[1].
      NULL,                 // Use parent's current directory.
      &startupInfo,         // STARTUPINFO pointer.
      &processInfo);        // PROCESS_INFORMATION pointer.

  // Release memory taken by the `arguments` string.
  delete arguments;

  if (!createProcessResult) {
    return WindowsError("CreateChildProcess: failed to call 'CreateProcess'");
  }

  // Close handles to child process/main thread and return child process PID
  ::CloseHandle(processInfo.hProcess);
  ::CloseHandle(processInfo.hThread);
  return processInfo.dwProcessId;
}
}  // namespace internal {

Subprocess::IO Subprocess::PIPE()
{
  return Subprocess::IO(
      []() -> Try<InputFileDescriptors> {
        HANDLE inHandle[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
        // Create STDIN pipe and set the 'write' component to not be
        // inheritable.
        const Try<Nothing> result = internal::CreatePipeHandles(inHandle);
        if (result.isError()) {
          return Error("PIPE: Could not create stdin pipe: " + result.error());
        }
        if (!::SetHandleInformation(&inHandle[1], HANDLE_FLAG_INHERIT, 0)) {
          return WindowsError("PIPE: Failed to call SetHandleInformation"
            " on stdin pipe");
        }

        InputFileDescriptors inDescriptors;
        inDescriptors.read = inHandle[0];
        inDescriptors.write = inHandle[1];
        return inDescriptors;
      },
      []() -> Try<OutputFileDescriptors> {
        HANDLE outHandle[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
        // Create OUT pipe and set the 'read' component to not be inheritable.
        const Try<Nothing> result = internal::CreatePipeHandles(outHandle);
        if (result.isError()) {
          return Error("PIPE: Could not create out pipe: " + result.error());
        }
        if (!::SetHandleInformation(&outHandle[0], HANDLE_FLAG_INHERIT, 0)) {
          return WindowsError("PIPE: Failed to call SetHandleInformation"
            " on out pipe");
        }

        OutputFileDescriptors outDescriptors;
        outDescriptors.read = outHandle[0];
        outDescriptors.write = outHandle[1];
        return outDescriptors;
      });
}


Subprocess::IO Subprocess::PATH(const string& path)
{
  return Subprocess::IO(
      [path]() -> Try<InputFileDescriptors> {
        // Get a handle to the `stdin` file. Use `GENERIC_READ` and
        // `FILE_SHARE_READ` to make the handle read/only (as `stdin` should
        // be), but allow others to read from the same file.
        HANDLE inHandle = ::CreateFile(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (inHandle == INVALID_HANDLE_VALUE) {
          return WindowsError("Failed to open '" + path + "'");
        }

        InputFileDescriptors inDescriptors;
        inDescriptors.read = inHandle;
        return inDescriptors;
      },
      [path]() -> Try<OutputFileDescriptors> {
        // Get a handle to the `stdout` file. Use `GENERIC_WRITE` to make the
        // handle writeable (as `stdout` should be) and do not allow others to
        // share the file.
        HANDLE outHandle = ::CreateFile(
            path.c_str(),
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (outHandle == INVALID_HANDLE_VALUE) {
          return WindowsError("Failed to open '" + path + "'");
        }

        OutputFileDescriptors outDescriptors;
        outDescriptors.write = outHandle;
        return outDescriptors;
      });
}


Subprocess::IO Subprocess::FD(int fd, IO::FDType type)
{
  return Subprocess::IO(
      [fd, type]() -> Try<InputFileDescriptors> {
        HANDLE inHandle = INVALID_HANDLE_VALUE;
        switch (type) {
          case IO::DUPLICATED:
          case IO::OWNED:
            // Extract handle from file descriptor.
            inHandle = (HANDLE)::_get_osfhandle(fd);
            if (inHandle == INVALID_HANDLE_VALUE) {
              return WindowsError("Failed to get handle of stdin file");
            }
            if (type == IO::DUPLICATED) {
              HANDLE duplicateHandle = INVALID_HANDLE_VALUE;

              // Duplicate the handle. See MSDN documentation[1] for details
              // on `DuplicateHandle` arguments and some sample code.
              //
              // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/ms724251(v=vs.85).aspx
              //
              // TODO(anaparu): Do we need to scope the duplicated handle
              // to the child process?
              const BOOL result = ::DuplicateHandle(
                  ::GetCurrentProcess(),    // Source process == current.
                  inHandle,                 // Handle to duplicate.
                  ::GetCurrentProcess(),    // Target process == current.
                  &duplicateHandle,
                  0,                        // Ignored (DUPLICATE_SAME_ACCESS).
                  TRUE,                     // Inheritable handle.
                  DUPLICATE_SAME_ACCESS);   // Same access level as source.

              if (!result) {
                return WindowsError("Failed to duplicate handle of stdin file");
              }

              inHandle = duplicateHandle;
            }
            break;
          // NOTE: By not setting a default we leverage the compiler
          // errors when the enumeration is augmented to find all
          // the cases we need to provide.  Same for below.
        }

        InputFileDescriptors inDescriptors;
        inDescriptors.read = inHandle;
        return inDescriptors;
      },
      [fd, type]() -> Try<OutputFileDescriptors> {
        HANDLE outHandle = INVALID_HANDLE_VALUE;
        switch (type) {
          case IO::DUPLICATED:
          case IO::OWNED:
            // Extract handle from file descriptor.
            outHandle = (HANDLE)::_get_osfhandle(fd);
            if (outHandle == INVALID_HANDLE_VALUE) {
              return WindowsError("Failed to get handle of output file");
            }
            if (type == IO::DUPLICATED) {
              HANDLE duplicateHandle = INVALID_HANDLE_VALUE;

              const BOOL result = ::DuplicateHandle(
                  ::GetCurrentProcess(),    // Source process == current.
                  outHandle,                // Handle to duplicate.
                  ::GetCurrentProcess(),    // Target process == current.
                  &duplicateHandle,
                  0,                        // Ignored (DUPLICATE_SAME_ACCESS).
                  TRUE,                     // Inheritable handle.
                  DUPLICATE_SAME_ACCESS);   // Same access level as source.

              if (!result) {
                return WindowsError("Failed to duplicate handle of out file");
              }

              outHandle = duplicateHandle;
            }
            break;
          }

        OutputFileDescriptors outDescriptors;
        outDescriptors.write = outHandle;
        return outDescriptors;
      });
}

Try<Subprocess> subprocess(
    const string& path,
    vector<string> argv,
    const Subprocess::IO& in,
    const Subprocess::IO& out,
    const Subprocess::IO& err,
    const Option<flags::FlagsBase>& flags,
    const Option<map<string, string>>& environment,
    const Option<lambda::function<int()>>& setup,
    const Option<lambda::function<
        pid_t(const lambda::function<int()>&)>>& _clone)
{
  // File descriptors for redirecting stdin/stdout/stderr.
  // These file descriptors are used for different purposes depending
  // on the specified I/O modes.
  // See `Subprocess::PIPE`, `Subprocess::PATH`, and `Subprocess::FD`.
  //
  // All these handles need to be closed before exiting the function on an
  // error condition. While RAII handles would make the code cleaner, we chose
  // to use internal::close() instead on all error paths. This is because on
  // success some of the handles (stdin-write, stdout-read, stderr-read) need
  // to remain open for the child process to use.
  InputFileDescriptors stdinfds;
  OutputFileDescriptors stdoutfds;
  OutputFileDescriptors stderrfds;

  // Prepare the file descriptor(s) for stdin.
  Try<InputFileDescriptors> input = in.input();
  if (input.isError()) {
    return Error(input.error());
  }

  stdinfds = input.get();

  // Prepare the file descriptor(s) for stdout.
  Try<OutputFileDescriptors> output = out.output();
  if (output.isError()) {
    internal::close(stdinfds, stdoutfds, stderrfds);
    return Error(output.error());
  }

  stdoutfds = output.get();

  // Prepare the file descriptor(s) for stderr.
  output = err.output();
  if (output.isError()) {
    internal::close(stdinfds, stdoutfds, stderrfds);
    return Error(output.error());
  }

  stderrfds = output.get();

  // Prepare the arguments. If the user specifies the 'flags', we will
  // stringify them and append them to the existing arguments.
  if (flags.isSome()) {
    foreachpair (const string& name, const flags::Flag& flag, flags.get()) {
      Option<string> value = flag.stringify(flags.get());
      if (value.isSome()) {
        argv.push_back("--" + name + "=" + value.get());
      }
    }
  }

  // Construct the environment that we'll pass to `CreateProcess`
  char** envp = os::raw::environment();

  if (environment.isSome()) {
    // According to MSDN[1], the `lpEnvironment` argument of `CreateProcess`
    // takes a null-terminated array of null-terminated strings. Each of these
    // strings must follow the `name=value\0` format.
    envp = new char*[environment.get().size() + 1];

    size_t index = 0;
    foreachpair(const string& key, const string& value, environment.get()) {
      string entry = key + "=" + value;
      envp[index] = new char[entry.size() + 1];
      strncpy(envp[index], entry.c_str(), entry.size() + 1);
      ++index;
    }

    // NULL-terminate the array of strings.
    envp[index] = NULL;
  }

  // Create the child process and pass the stdin/stdout/stderr handles.
  Try<pid_t> pid = internal::CreateChildProcess(
      path,
      argv,
      (LPVOID)envp,
      stdinfds,
      stdoutfds,
      stderrfds);

  // Need to delete 'envp' if we had environment variables passed to
  // us and we needed to allocate the space.
  if (environment.isSome()) {
    CHECK_NE(os::raw::environment(), envp);
    delete[] envp;
  }

  if (pid.isError()) {
    internal::close(stdinfds, stdoutfds, stderrfds);
    return Error("Could not launch child process" + pid.error());
  }

  if (pid.get() == -1) {
    // Save the errno as 'close' below might overwrite it.
    ErrnoError error("Failed to clone");
    internal::close(stdinfds, stdoutfds, stderrfds);
    return error;
  }

  Subprocess process;
  process.data->pid = pid.get();

  // Close the handles that are created by this function. For pipes, we close
  // the child ends and store the parent ends (see thecode below).
  ::CloseHandle(stdinfds.read);
  ::CloseHandle(stdoutfds.write);
  ::CloseHandle(stderrfds.write);

  // If the mode is PIPE, store the parent side of the pipe so that
  // the user can communicate with the subprocess. Windows uses handles for all
  // of these, so we need to associate them to file descriptors first.
  process.data->in = ::_open_osfhandle(
      (intptr_t)stdinfds.write.getOrElse(INVALID_HANDLE_VALUE),
      _O_APPEND | _O_TEXT);

  process.data->out = ::_open_osfhandle(
      (intptr_t)stdoutfds.read.getOrElse(INVALID_HANDLE_VALUE),
      _O_RDONLY | _O_TEXT);

  process.data->err = ::_open_osfhandle(
      (intptr_t)stderrfds.read.getOrElse(INVALID_HANDLE_VALUE),
      _O_RDONLY | _O_TEXT);

  // Rather than directly exposing the future from process::reap, we
  // must use an explicit promise so that we can ensure we can receive
  // the termination signal. Otherwise, the caller can discard the
  // reap future, and we will not know when it is safe to close the
  // file descriptors.
  Promise<Option<int>>* promise = new Promise<Option<int>>();
  process.data->status = promise->future();

  // We need to bind a copy of this Subprocess into the onAny callback
  // below to ensure that we don't close the file descriptors before
  // the subprocess has terminated (i.e., because the caller doesn't
  // keep a copy of this Subprocess around themselves).
  process::reap(process.data->pid)
    .onAny(lambda::bind(internal::cleanup, lambda::_1, promise, process));

  return process;
}

}  // namespace process {
