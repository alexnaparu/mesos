// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __STOUT_WINDOWS_OS_HPP__
#define __STOUT_WINDOWS_OS_HPP__

#include <direct.h>
#include <io.h>
#include <psapi.h>

#include <sys/utime.h>

#include <list>
#include <map>
#include <set>
#include <string>

#include <stout/duration.hpp>
#include <stout/none.hpp>
#include <stout/nothing.hpp>
#include <stout/option.hpp>
#include <stout/path.hpp>
#include <stout/try.hpp>
#include <stout/windows.hpp>

#include <stout/os/os.hpp>
#include <stout/os/raw/environment.hpp>

namespace os {


/*
// Sets the value associated with the specified key in the set of
// environment variables.
inline void setenv(const std::string& key,
                   const std::string& value,
                   bool overwrite = true)
{
  UNIMPLEMENTED;
}


// Unsets the value associated with the specified key in the set of
// environment variables.
inline void unsetenv(const std::string& key)
{
  UNIMPLEMENTED;
}


// Executes a command by calling "/bin/sh -c <command>", and returns
// after the command has been completed. Returns 0 if succeeds, and
// return -1 on error (e.g., fork/exec/waitpid failed). This function
// is async signal safe. We return int instead of returning a Try
// because Try involves 'new', which is not async signal safe.
inline int system(const std::string& command)
{
  UNIMPLEMENTED;
}


// This function is a portable version of execvpe ('p' means searching
// executable from PATH and 'e' means setting environments). We add
// this function because it is not available on all systems.
//
// NOTE: This function is not thread safe. It is supposed to be used
// only after fork (when there is only one thread). This function is
// async signal safe.
inline int execvpe(const char* file, char** argv, char** envp)
{
  UNIMPLEMENTED;
}


inline Try<Nothing> chown(
    uid_t uid,
    gid_t gid,
    const std::string& path,
    bool recursive)
{
  UNIMPLEMENTED;
}


inline Try<Nothing> chmod(const std::string& path, int mode)
{
  UNIMPLEMENTED;
}


inline Try<Nothing> chroot(const std::string& directory)
{
  UNIMPLEMENTED;
}


inline Try<Nothing> mknod(
    const std::string& path,
    mode_t mode,
    dev_t dev)
{
  UNIMPLEMENTED;
}


inline Result<uid_t> getuid(const Option<std::string>& user = None())
{
  UNIMPLEMENTED;
}


inline Result<gid_t> getgid(const Option<std::string>& user = None())
{
  UNIMPLEMENTED;
}


inline Try<Nothing> su(const std::string& user)
{
  UNIMPLEMENTED;
}


inline Result<std::string> user(Option<uid_t> uid = None())
{
  UNIMPLEMENTED;
}
*/
// Suspends execution for the given duration.
inline Try<Nothing> sleep(const Duration& duration)
{
  DWORD milliseconds = static_cast<DWORD>(duration.ms());
  ::Sleep(milliseconds);
}

// Returns the total number of cpus (cores).
inline Try<long> cpus()
{
  SYSTEM_INFO sysInfo;
  ::GetSystemInfo(&sysInfo);
  return static_cast<long>(sysInfo.dwNumberOfProcessors);
}

/*
// Returns load struct with average system loads for the last
// 1, 5 and 15 minutes respectively.
// Load values should be interpreted as usual average loads from
// uptime(1).
inline Try<Load> loadavg()
{
  UNIMPLEMENTED;
}
*/

// Returns the total size of main and free memory.
inline Try<Memory> memory()
{
  Memory memory;

  MEMORYSTATUSEX memoryStatus;
  memoryStatus.dwLength = sizeof(MEMORYSTATUSEX);
  if (!GlobalMemoryStatusEx(&memoryStatus)) {
    return WindowsError("`memory()`: Could not call `GlobalMemoryStatusEx`");
  }

  memory.total = Bytes(memoryStatus.ullTotalPhys);
  memory.free = Bytes(memoryStatus.ullAvailPhys);
  memory.totalSwap = Bytes(memoryStatus.ullTotalPageFile);
  memory.freeSwap = Bytes(memoryStatus.ullAvailPageFile);

  return memory;
}

// Return the system information.
inline Try<UTSInfo> uname()
{
  UTSInfo info;

  OSVERSIONINFOEX osVersion;
  osVersion.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
  if (!::GetVersionEx((LPOSVERSIONINFO)&osVersion)) {
    return WindowsError("`os::uname()`: Failed to call `GetVersionEx`");
  }

  switch(osVersion.wProductType) {
    case VER_NT_DOMAIN_CONTROLLER:
    case VER_NT_SERVER:
      info.sysname = "Windows Server";
      break;
    default:
      info.sysname = "Windows";
  }

  info.release = std::to_string(osVersion.dwMajorVersion) + "." +
      std::to_string(osVersion.dwMinorVersion);
  info.version = std::to_string(osVersion.dwBuildNumber);
  if (osVersion.szCSDVersion[0] != '\0') {
    info.version.append(" ");
    info.version.append(osVersion.szCSDVersion);
  }

  // Get DNS name of the local computer. First, find the size of the output
  // buffer.
  DWORD size = 0;
  if (!::GetComputerNameEx(ComputerNameDnsHostname, NULL, &size) &&
      ::GetLastError() != ERROR_MORE_DATA) {
    return WindowsError("`os::uname()`: Failed to call `GetComputerNameEx`");
  }

  std::shared_ptr<char> computerName(
      (char *)malloc((size + 1) * sizeof(char)));

  if (!::GetComputerNameEx(ComputerNameDnsHostname, computerName.get(),
      &size)) {
    return WindowsError("`os::uname()`: Failed to call `GetComputerNameEx`");
  }

  info.nodename = computerName.get();

  // Get OS architecture
  SYSTEM_INFO systemInfo;
  ::GetNativeSystemInfo(&systemInfo);
  switch(systemInfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
      info.machine = "AMD64";
      break;
    case PROCESSOR_ARCHITECTURE_ARM:
      info.machine = "ARM";
      break;
    case PROCESSOR_ARCHITECTURE_IA64:
      info.machine = "IA64";
      break;
    case PROCESSOR_ARCHITECTURE_INTEL:
      info.machine = "x86";
      break;
    default:
      info.machine = "Unknown";
  }

  return info;
}

/*
inline Try<std::list<Process>> processes()
{
  UNIMPLEMENTED;
}
*/

// Overload of os::pids for filtering by groups and sessions.
// A group / session id of 0 will fitler on the group / session ID
// of the calling process.
inline Try<std::set<pid_t>> pids(Option<pid_t> group, Option<pid_t> session)
{
  // Windows does not have the concept of a process group, so we need to
  // enumerate all processes.
  //
  // The list of processes might differ between calls, so continue calling
  // `EnumProcesses` until the output buffer is large enough. The call is
  // considered to fully succeed when the function returns non-zero and the
  // number of bytes returned is less than the size of the `pids` array. If
  // that's not the case, then we need to increase the size of the `pids` array
  // and attempt the call again.
  //
  // To minimize the number of calls (at the expense
  // or memory), we choose to allocate double the amount suggested by
  // `EnumProcesses`.
  DWORD *pids = NULL;
  DWORD bytes = 1024;
  DWORD pidsSize = 0;

  // TODO(alexnaparu): Set a limit to the memory that can be used.
  while (pidsSize <= bytes) {
    pidsSize = 2 * bytes;
    pids = (DWORD *)realloc(pids, pidsSize);
    if (!::EnumProcesses(pids, pidsSize, &bytes)) {
      free(pids);
      return WindowsError("`os::pids()`: Failed to call `EnumProcesses`");
    }
  }

  std::set<pid_t> result;
  for (DWORD i = 0; i < bytes / sizeof(DWORD); i++) {
    result.insert(pids[i]);
  }

  free(pids);
  return result;
}
} // namespace os {

#endif // __STOUT_WINDOWS_OS_HPP__
