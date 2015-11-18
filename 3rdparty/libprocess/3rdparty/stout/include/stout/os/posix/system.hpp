/**
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __STOUT_OS_POSIX_SYSTEM_HPP__
#define __STOUT_OS_POSIX_SYSTEM_HPP__

#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace os {

// Executes a command by calling "/bin/sh -c <command>", and returns
// after the command has been completed. Returns 0 if succeeds, and
// return -1 on error (e.g., fork/exec/waitpid failed). This function
// is async signal safe. We return int instead of returning a Try
// because Try involves 'new', which is not async signal safe.
inline int system(const std::string& command)
{
  pid_t pid = ::fork();

  if (pid == -1) {
    return -1;
  } else if (pid == 0) {
    // In child process.
    ::execlp("sh", "sh", "-c", command.c_str(), (char*) NULL);
    ::exit(127);
  } else {
    // In parent process.
    int status;
    while (::waitpid(pid, &status, 0) == -1) {
      if (errno != EINTR) {
        return -1;
      }
    }

    return status;
  }
}
} // namespace os {

#endif // __STOUT_OS_POSIX_SYSTEM_HPP__
