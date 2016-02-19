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

#include <signal.h>
#include <stdio.h>

#include <iostream>
#include <list>
#include <string>
#include <vector>

#include <mesos/executor.hpp>
#include <mesos/type_utils.hpp>

#include <process/defer.hpp>
#include <process/delay.hpp>
#include <process/future.hpp>
#include <process/io.hpp>
#include <process/pipe.hpp>
#include <process/process.hpp>
#include <process/protobuf.hpp>
#include <process/subprocess.hpp>
#include <process/reap.hpp>
#include <process/timer.hpp>

#ifdef __WINDOWS__
#include <process/windows/winsock.hpp>    // WSAStartup code.
#endif // __WINDOWS__

#include <stout/duration.hpp>
#include <stout/flags.hpp>
#include <stout/json.hpp>
#include <stout/lambda.hpp>
#include <stout/option.hpp>
#include <stout/os.hpp>
#include <stout/path.hpp>
#include <stout/protobuf.hpp>
#include <stout/strings.hpp>
#include <stout/os/kill.hpp>
#include <stout/os/killtree.hpp>

#include "common/http.hpp"
#include "common/status_utils.hpp"

#ifdef __linux__
#include "linux/fs.hpp"
#endif

#include "logging/logging.hpp"

#include "messages/messages.hpp"

#include "slave/constants.hpp"
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


void CommandExecutorProcess::registered(
    ExecutorDriver* _driver,
    const ExecutorInfo& _executorInfo,
    const FrameworkInfo& frameworkInfo,
    const SlaveInfo& slaveInfo)
{
  CHECK_EQ(REGISTERING, state);

  cout << "Registered executor on " << slaveInfo.hostname() << endl;
  driver = _driver;
  state = REGISTERED;
}

void CommandExecutorProcess::reregistered(
    ExecutorDriver* driver,
    const SlaveInfo& slaveInfo)
{
  CHECK(state == REGISTERED || state == REGISTERING) << state;

  cout << "Re-registered executor on " << slaveInfo.hostname() << endl;
  state = REGISTERED;
}


void CommandExecutorProcess::launchTask(
    ExecutorDriver* driver,
    const TaskInfo& task)
{
  CHECK_EQ(REGISTERED, state);

  if (launched) {
    TaskStatus status;
    status.mutable_task_id()->MergeFrom(task.task_id());
    status.set_state(TASK_FAILED);
    status.set_message(
        "Attempted to run multiple tasks using a \"command\" executor");

    driver->sendStatusUpdate(status);
    return;
  }

  // Determine the command to launch the task.
  CommandInfo command;

  if (taskCommand.isSome()) {
    // Get CommandInfo from a JSON string.
    Try<JSON::Object> object = JSON::parse<JSON::Object>(taskCommand.get());
    if (object.isError()) {
      cerr << "Failed to parse JSON: " << object.error() << endl;
      abort();
    }

    Try<CommandInfo> parse = protobuf::parse<CommandInfo>(object.get());
    if (parse.isError()) {
      cerr << "Failed to parse protobuf: " << parse.error() << endl;
      abort();
    }

    command = parse.get();
  } else if (task.has_command()) {
    command = task.command();
  } else {
    CHECK_SOME(override)
      << "Expecting task '" << task.task_id()
      << "' to have a command!";
  }

  if (override.isNone()) {
    // TODO(jieyu): For now, we just fail the executor if the task's
    // CommandInfo is not valid. The framework will receive
    // TASK_FAILED for the task, and will most likely find out the
    // cause with some debugging. This is a temporary solution. A more
    // correct solution is to perform this validation at master side.
    if (command.shell()) {
      CHECK(command.has_value())
        << "Shell command of task '" << task.task_id()
        << "' is not specified!";
    } else {
      CHECK(command.has_value())
        << "Executable of task '" << task.task_id()
        << "' is not specified!";
    }
  }

  cout << "Starting task " << task.task_id() << endl;

  // Prepare the argv before fork as it's not async signal safe.
  char **argv = new char*[command.arguments().size() + 1];
  for (int i = 0; i < command.arguments().size(); i++) {
    argv[i] = (char*)command.arguments(i).c_str();
  }
  argv[command.arguments().size()] = NULL;

  // Prepare the command log message.
  string commandString;
  if (override.isSome()) {
    char** argv = override.get();
    // argv is guaranteed to be NULL terminated and we rely on
    // that fact to print command to be executed.
    for (int i = 0; argv[i] != NULL; i++) {
      commandString += string(argv[i]) + " ";
    }
  }
  else if (command.shell()) {
    commandString = string(os::Shell::arg0) + " " +
      string(os::Shell::arg1) + " '" +
      command.value() + "'";
  }
  else {
    commandString =
      "[" + command.value() + ", " +
      strings::join(", ", command.arguments()) + "]";
  }

  cout << commandString << endl;

  // The actual work of launching the task is platform-specific and is done
  // in here.
  pid = _launchTask(task, command, (const char**)argv);

  delete[] argv;

  cout << "Forked command at " << pid << endl;

  launchHealthCheck(task);

  // Monitor this process.
  process::reap(pid)
    .onAny(defer(self(),
                  &Self::reaped,
                  driver,
                  task.task_id(),
                  pid,
                  lambda::_1));

  TaskStatus status;
  status.mutable_task_id()->MergeFrom(task.task_id());
  status.set_state(TASK_RUNNING);
  driver->sendStatusUpdate(status);

  launched = true;
}

void CommandExecutorProcess::killTask(
    ExecutorDriver* driver, const TaskID& taskId)
{
  shutdown(driver);
  if (healthPid != -1) {
    // Cleanup health check process.
    os::killtree(healthPid, SIGKILL);
  }
}

void CommandExecutorProcess::shutdown(ExecutorDriver* driver)
{
  cout << "Shutting down" << endl;

  if (pid > 0 && !killed) {
    cout << "Sending SIGTERM to process tree at pid "
          << pid << endl;

    Try<std::list<os::ProcessTree> > trees =
      os::killtree(pid, SIGTERM, true, true);

    if (trees.isError()) {
      cerr << "Failed to kill the process tree rooted at pid "
            << pid << ": " << trees.error() << endl;

      // Send SIGTERM directly to process 'pid' as it may not have
      // received signal before os::killtree() failed.
      os::kill(pid, SIGTERM);
    } else {
      cout << "Killing the following process trees:\n"
            << stringify(trees.get()) << endl;
    }

    // TODO(nnielsen): Make escalationTimeout configurable through
    // slave flags and/or per-framework/executor.
    escalationTimer = delay(
        escalationTimeout,
        self(),
        &Self::escalated);

    killed = true;
  }
}

void CommandExecutorProcess::initialize()
{
  install<TaskHealthStatus>(
      &CommandExecutorProcess::taskHealthUpdated,
      &TaskHealthStatus::task_id,
      &TaskHealthStatus::healthy,
      &TaskHealthStatus::kill_task);
}

void CommandExecutorProcess::taskHealthUpdated(
    const TaskID& taskID,
    const bool& healthy,
    const bool& initiateTaskKill)
{
  if (driver.isNone()) {
    return;
  }

  cout << "Received task health update, healthy: "
        << stringify(healthy) << endl;

  TaskStatus status;
  status.mutable_task_id()->CopyFrom(taskID);
  status.set_healthy(healthy);
  status.set_state(TASK_RUNNING);
  driver.get()->sendStatusUpdate(status);

  if (initiateTaskKill) {
    killedByHealthCheck = true;
    killTask(driver.get(), taskID);
  }
}


void CommandExecutorProcess::reaped(
    ExecutorDriver* driver,
    const TaskID& taskId,
    pid_t pid,
    const Future<Option<int> >& status_)
{
  TaskState taskState;
  string message;

  Clock::cancel(escalationTimer);

  if (!status_.isReady()) {
    taskState = TASK_FAILED;
    message =
      "Failed to get exit status for Command: " +
      (status_.isFailed() ? status_.failure() : "future discarded");
  } else if (status_.get().isNone()) {
    taskState = TASK_FAILED;
    message = "Failed to get exit status for Command";
  } else {
    int status = status_.get().get();
    CHECK(WIFEXITED(status) || WIFSIGNALED(status)) << status;

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      taskState = TASK_FINISHED;
    } else if (killed) {
      // Send TASK_KILLED if the task was killed as a result of
      // killTask() or shutdown().
      taskState = TASK_KILLED;
    } else {
      taskState = TASK_FAILED;
    }

    message = "Command " + WSTRINGIFY(status);
  }

  cout << message << " (pid: " << pid << ")" << endl;

  TaskStatus taskStatus;
  taskStatus.mutable_task_id()->MergeFrom(taskId);
  taskStatus.set_state(taskState);
  taskStatus.set_message(message);
  if (killed && killedByHealthCheck) {
    taskStatus.set_healthy(false);
  }

  driver->sendStatusUpdate(taskStatus);

  // This is a hack to ensure the message is sent to the
  // slave before we exit the process. Without this, we
  // may exit before libprocess has sent the data over
  // the socket. See MESOS-4111.
  os::sleep(Seconds(1));
  driver->stop();
}

void CommandExecutorProcess::escalated()
{
  cout << "Process " << pid << " did not terminate after "
        << escalationTimeout << ", sending SIGKILL to "
        << "process tree at " << pid << endl;

  // TODO(nnielsen): Sending SIGTERM in the first stage of the
  // shutdown may leave orphan processes hanging off init. This
  // scenario will be handled when PID namespace encapsulated
  // execution is in place.
  Try<std::list<os::ProcessTree> > trees =
    os::killtree(pid, SIGKILL, true, true);

  if (trees.isError()) {
    cerr << "Failed to kill the process tree rooted at pid "
          << pid << ": " << trees.error() << endl;

    // Process 'pid' may not have received signal before
    // os::killtree() failed. To make sure process 'pid' is reaped
    // we send SIGKILL directly.
    os::kill(pid, SIGKILL);
  } else {
    cout << "Killed the following process trees:\n" << stringify(trees.get())
          << endl;
  }
}

void CommandExecutorProcess::launchHealthCheck(const TaskInfo& task)
{
  if (task.has_health_check()) {
    JSON::Object json = JSON::protobuf(task.health_check());

    // Launch the subprocess using 'exec' style so that quotes can
    // be properly handled.
    vector<string> argv(4);
    argv[0] = "mesos-health-check";
    argv[1] = "--executor=" + stringify(self());
    argv[2] = "--health_check_json=" + stringify(json);
    argv[3] = "--task_id=" + task.task_id().value();

    cout << "Launching health check process: "
          << path::join(healthCheckDir, "mesos-health-check")
          << " " << argv[1] << " " << argv[2] << " " << argv[3] << endl;

    Try<Subprocess> healthProcess =
      process::subprocess(
        path::join(healthCheckDir, "mesos-health-check"),
        argv,
        // Intentionally not sending STDIN to avoid health check
        // commands that expect STDIN input to block.
        Subprocess::PATH("/dev/null"),
        Subprocess::FD(STDOUT_FILENO),
        Subprocess::FD(STDERR_FILENO));

    if (healthProcess.isError()) {
      cerr << "Unable to launch health process: " << healthProcess.error();
    } else {
      healthPid = healthProcess.get().pid();

      cout << "Health check process launched at pid: "
            << stringify(healthPid) << endl;
    }
  }
}


class CommandExecutor: public Executor
{
public:
  CommandExecutor(
      const Option<char**>& override,
      const string& healthCheckDir,
      const Option<string>& sandboxDirectory,
      const Option<string>& workingDirectory,
      const Option<string>& user,
      const Option<string>& taskCommand)
  {
    process = new CommandExecutorProcess(override,
                                         healthCheckDir,
                                         sandboxDirectory,
                                         workingDirectory,
                                         user,
                                         taskCommand);

    spawn(process);
  }

  virtual ~CommandExecutor()
  {
    terminate(process);
    wait(process);
    delete process;
  }

  virtual void registered(
        ExecutorDriver* driver,
        const ExecutorInfo& executorInfo,
        const FrameworkInfo& frameworkInfo,
        const SlaveInfo& slaveInfo)
  {
    dispatch(process,
             &CommandExecutorProcess::registered,
             driver,
             executorInfo,
             frameworkInfo,
             slaveInfo);
  }

  virtual void reregistered(
      ExecutorDriver* driver,
      const SlaveInfo& slaveInfo)
  {
    dispatch(process,
             &CommandExecutorProcess::reregistered,
             driver,
             slaveInfo);
  }

  virtual void disconnected(ExecutorDriver* driver)
  {
    dispatch(process, &CommandExecutorProcess::disconnected, driver);
  }

  virtual void launchTask(ExecutorDriver* driver, const TaskInfo& task)
  {
    dispatch(process, &CommandExecutorProcess::launchTask, driver, task);
  }

  virtual void killTask(ExecutorDriver* driver, const TaskID& taskId)
  {
    dispatch(process, &CommandExecutorProcess::killTask, driver, taskId);
  }

  virtual void frameworkMessage(ExecutorDriver* driver, const string& data)
  {
    dispatch(process, &CommandExecutorProcess::frameworkMessage, driver, data);
  }

  virtual void shutdown(ExecutorDriver* driver)
  {
    dispatch(process, &CommandExecutorProcess::shutdown, driver);
  }

  virtual void error(ExecutorDriver* driver, const string& data)
  {
    dispatch(process, &CommandExecutorProcess::error, driver, data);
  }

private:
  CommandExecutorProcess* process;
};

} // namespace internal {
} // namespace mesos {


class Flags : public flags::FlagsBase
{
public:
  Flags()
  {
    // TODO(gilbert): Deprecate the 'override' flag since no one is
    // using it, and it may cause confusing with 'task_command' flag.
    add(&override,
        "override",
        "Whether to override the command the executor should run when the\n"
        "task is launched. Only this flag is expected to be on the command\n"
        "line and all arguments after the flag will be used as the\n"
        "subsequent 'argv' to be used with 'execvp'",
        false);

    // The following flags are only applicable when a rootfs is
    // provisioned for this command.
    add(&sandbox_directory,
        "sandbox_directory",
        "The absolute path for the directory in the container where the\n"
        "sandbox is mapped to");

    add(&working_directory,
        "working_directory",
        "The working directory for the task in the container.");

    add(&user,
        "user",
        "The user that the task should be running as.");

    add(&task_command,
        "task_command",
        "If specified, this is the overrided command for launching the\n"
        "task (instead of the command from TaskInfo).");

    // TODO(nnielsen): Add 'prefix' option to enable replacing
    // 'sh -c' with user specified wrapper.
  }

  bool override;
  Option<string> sandbox_directory;
  Option<string> working_directory;
  Option<string> user;
  Option<string> task_command;
};

int main(int argc, char** argv)
{
  Flags flags;

#ifdef __WINDOWS__
  process::Winsock winsock;
#endif

  // Load flags from command line.
  Try<Nothing> load = flags.load(None(), &argc, &argv);

  if (load.isError()) {
    cerr << flags.usage(load.error()) << endl;
    return EXIT_FAILURE;
  }

  if (flags.help) {
    cout << flags.usage() << endl;
    return EXIT_SUCCESS;
  }

  // After flags.load(..., &argc, &argv) all flags will have been
  // stripped from argv. Additionally, arguments after a "--"
  // terminator will be preservered in argv and it is therefore
  // possible to pass override and prefix commands which use
  // "--foobar" style flags.
  Option<char**> override = None();
  if (flags.override) {
    if (argc > 1) {
      override = argv + 1;
    }
  }

  const Option<string> envPath = os::getenv("MESOS_LAUNCHER_DIR");

  string path = envPath.isSome()
    ? envPath.get()
    : os::realpath(Path(argv[0]).dirname()).get();

  mesos::internal::CommandExecutor executor(
      override,
      path,
      flags.sandbox_directory,
      flags.working_directory,
      flags.user,
      flags.task_command);

  mesos::MesosExecutorDriver driver(&executor);
  int result =
      (driver.run() == mesos::DRIVER_STOPPED ? EXIT_SUCCESS : EXIT_FAILURE);

  return result;
}
