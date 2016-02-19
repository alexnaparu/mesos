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

#pragma once

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

// Windows defines from NetBios header
#ifdef __WINDOWS__
#ifdef REGISTERING
#undef REGISTERING
#endif // REGISTERING
#ifdef REGISTERED
#undef REGISTERED
#endif // REGISTERED
#endif // __WINDOWS__

using namespace mesos::internal::slave;

using process::wait; // Necessary on some OS's to disambiguate.

using std::string;
using std::vector;

namespace mesos {
namespace internal {

using namespace process;


class CommandExecutorProcess : public ProtobufProcess<CommandExecutorProcess>
{
public:
  CommandExecutorProcess(
      const Option<char**>& override,
      const string& _healthCheckDir,
      const Option<string>& _sandboxDirectory,
      const Option<string>& _workingDirectory,
      const Option<string>& _user,
      const Option<string>& _taskCommand)
    : state(REGISTERING),
      launched(false),
      killed(false),
      killedByHealthCheck(false),
      pid(-1),
      healthPid(-1),
      escalationTimeout(slave::EXECUTOR_SIGNAL_ESCALATION_TIMEOUT),
      driver(None()),
      healthCheckDir(_healthCheckDir),
      override(override),
      sandboxDirectory(_sandboxDirectory),
      workingDirectory(_workingDirectory),
      user(_user),
      taskCommand(_taskCommand) {}

  virtual ~CommandExecutorProcess() {}

  void registered(
      ExecutorDriver* _driver,
      const ExecutorInfo& _executorInfo,
      const FrameworkInfo& frameworkInfo,
      const SlaveInfo& slaveInfo);

  void reregistered(
      ExecutorDriver* driver,
      const SlaveInfo& slaveInfo);

  void disconnected(ExecutorDriver* driver) {}

  void launchTask(ExecutorDriver* driver, const TaskInfo& task);

  void killTask(ExecutorDriver* driver, const TaskID& taskId);

  void frameworkMessage(ExecutorDriver* driver, const string& data) {}

  void shutdown(ExecutorDriver* driver);

  virtual void error(ExecutorDriver* driver, const string& message) {}

protected:
  virtual void initialize();

  void taskHealthUpdated(
      const TaskID& taskID,
      const bool& healthy,
      const bool& initiateTaskKill);

private:
  void reaped(
      ExecutorDriver* driver,
      const TaskID& taskId,
      pid_t pid,
      const Future<Option<int> >& status_);

  void escalated();

  void launchHealthCheck(const TaskInfo& task);

  // Platform-specific method that does the actual work of launching a task.
  pid_t _launchTask(
    const TaskInfo& task,
    const CommandInfo& command,
    const char** argv);

  enum State
  {
    REGISTERING, // Executor is launched but not (re-)registered yet.
    REGISTERED,  // Executor has (re-)registered.
  } state;

  bool launched;
  bool killed;
  bool killedByHealthCheck;
  pid_t pid;
  pid_t healthPid;
  Duration escalationTimeout;
  Timer escalationTimer;
  Option<ExecutorDriver*> driver;
  string healthCheckDir;
  Option<char**> override;
  Option<string> sandboxDirectory;
  Option<string> workingDirectory;
  Option<string> user;
  Option<string> taskCommand;
};

} // namespace internal {
} // namespace mesos {
