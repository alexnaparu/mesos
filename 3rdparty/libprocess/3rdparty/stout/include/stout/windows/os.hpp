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

#include <stout/os/config.hpp>
#include <stout/os/os.hpp>
#include <stout/os/read.hpp>

#include <stout/os/raw/environment.hpp>


#define WNOHANG 0
#define hstrerror() ("")
#define SIGPIPE 100

namespace os {

  inline int pagesize()
  {
    SYSTEM_INFO si = {0};
    GetSystemInfo(&si);
    return si.dwPageSize;
  };

  inline long cpu()
  {
    return 4;
  };

// Sets the value associated with the specified key in the set of
// environment variables.
inline void setenv(const std::string& key,
                   const std::string& value,
                   bool overwrite = true)
{
}

/*
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
*/

#define WNOHANG     1				/* dont hang in wait */
#define WUNTRACED   2				/* tell about stopped, untraced children */

#define WIFEXITED(x) true			/* whether the child terminated normally */
#define WIFSIGNALED(x) false		/* whether the child was terminated by a signal */
#define WCOREDUMP(x) false			/* whether the child produced a core dump, only be used if WIFSIGNALED is true*/
#define WIFSTOPPED(x) false			/* whether the child was stopped by delivery of a signal */

#define WEXITSTATUS(x) (x & 0xFF)	/* returns the exit status of the child, only be used if WIFEXITED is true */
#define WTERMSIG(x) 0				/* returns the number of the signals that caused the child process to terminate,
									   only be used if WIFSIGNALED is true */

  // Suspends execution of the calling process until a child specified
  // by pid argument has changed state. By default, waitpid() waits only
  // for termninated children, but this behavior is modifiable via the
  // options argument
  //
  // The value of pid can be:
  // <-1: meaning wait for any child process whose process group ID is equal to the absolute value of pid.
  // -1: meaning wait for any child process.
  // 0: meaning wait for any child process whose process group ID is equal to that of the calling process.
  // >0: meaning wait for the child whose process ID is equal to the value of pid.
  //
  // The value of options is an OR of zero or more of the following constants:
  // WNOHANG: return immediately if no child has exited.
  // WUNTRACED: also return if a child has stopped (but not traced via ptrace(2)). Status for traced children
  //		which have stopped is provided even if this option is not specified.
  //
  // If status is not NULL, waitpid() stores status information in the int to which it points.
  //
  // Returns a value equal to the process ID of the child process for which status is reported. If the status
  // is not available, 0 is returned. Otherwise, -1 shall be returend and errno set to indicate the error.

  inline pid_t waitpid(pid_t pid, int *status, int options)
  {
	  // For now, we only implement: pid > 0 && options = 0
	  if ((pid <= 0) || (options != 0 && options != WNOHANG))
	  {
		  // Function not implemented
		  errno = ENOSYS;
		  return -1;
	  }

	  // TODO(yisun) : check pid is one of the child processes
	  // if not, set errno to ECHILD and return -1

	  // Open the child process
	  HANDLE hProcess;
	  hProcess = ::OpenProcess(
		  PROCESS_QUERY_INFORMATION | SYNCHRONIZE,
		  FALSE,
		  static_cast<DWORD>(pid));

	  // Error out if not able to open
	  if (hProcess == NULL)
	  {
		  // Failed to open the child process
		  errno = ECHILD;
		  return -1;
	  }
	  std::shared_ptr<void> hSafeProcess(hProcess, ::CloseHandle);

	  // Wait for child to terminate by default
	  // otherwise (WNOHANG), no wait
	  DWORD dwMilliseconds = (options == 0) ? INFINITE : 0;

	  // Wait for the child process
	  DWORD dwRes = ::WaitForSingleObject(hSafeProcess.get(), dwMilliseconds);

	  // Error out if wait failed
	  if ((options == 0 && dwRes != WAIT_OBJECT_0) ||
		  (options == WNOHANG && dwRes != WAIT_OBJECT_0 && dwRes != WAIT_TIMEOUT))
	  {
		  // Failed to wait the child process
		  errno = ECHILD;
		  return -1;
	  }

	  // Child not terminated yet in the case of WNOHANG
	  if (dwRes == WAIT_TIMEOUT)
	  {
		  return 0;
	  }

	  // dwRes == WAIT_OBJECT_0: retrieve the process termination status
	  DWORD dwExitCode = 0;
	  if (!::GetExitCodeProcess(hSafeProcess.get(), &dwExitCode))
	  {
		  // Failed to retrieve the status
		  errno = ECHILD;
		  return -1;
	  }

	  // Return the exit code in status
	  if (status != NULL)
	  {
		  *status = dwExitCode;
	  }

	  // Return the pid of the child process for which the status is reported
	  return pid;
  }

/*
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
// Returns the list of files that match the given (shell) pattern.
inline Try<std::list<std::string>> glob(const std::string& pattern)
{
UNIMPLEMENTED;
}
*/

// Suspends execution for the given duration.
inline Try<Nothing> sleep(const Duration& duration)
{
  return Nothing();
}




// Returns the total number of cpus (cores).
inline Try<long> cpus()
{
  return 4;
}


// Returns load struct with average system loads for the last
// 1, 5 and 15 minutes respectively.
// Load values should be interpreted as usual average loads from
// uptime(1).
inline Try<Load> loadavg()
{
  return Load();
}


// Returns the total size of main and free memory.
inline Try<Memory> memory()
{
  return Memory();
}


// Return the system information.
inline Try<UTSInfo> uname()
{
  return UTSInfo();
}


inline Try<std::list<Process>> processes()
{
  return std::list<Process>();
}

inline size_t recv(int sockfd, void *buf, size_t len, int flags) {
  return ::recv(sockfd, (char*)buf, len, flags);
}

inline int setsockopt(int socket, int level, int option_name,
       const void *option_value, socklen_t option_len) {
  return ::setsockopt(socket, level, option_name, (const char*)option_value, option_len);
}

inline int getsockopt(int socket, int level, int option_name,
  void* option_value, socklen_t* option_len) {
  return ::getsockopt(socket, level, option_name, (char*)option_value, option_len);
}

// Looks in the environment variables for the specified key and
// returns a string representation of its value. If no environment
// variable matching key is found, None() is returned.
inline Option<std::string> getenv(const std::string& key)
{
  char* value = ::getenv(key.c_str());

  if (value == NULL) {
    return None();
  }

  return std::string(value);
}

inline struct tm* gmtime_r(const time_t *timep, struct tm *result)
{
  if (gmtime_s(result, timep))
  {
    return result;
  }

  return NULL;
}

inline pid_t waitpid(pid_t pid, int *status, int options)
{
    return 0;
}

namespace libraries {

  // Returns the full library name by adding prefix and extension to
  // library name.
  inline std::string expandName(const std::string& libraryName)
  {
    const char* prefix = "lib";
    const char* extension =
#ifdef __APPLE__
      ".dylib";
#else
      ".so";
#endif

    return prefix + libraryName + extension;
  }


  // Returns the current value of LD_LIBRARY_PATH environment variable.
  inline std::string paths()
  {
    const char* environmentVariable =
#ifdef __APPLE__
      "DYLD_LIBRARY_PATH";
#else
      "LD_LIBRARY_PATH";
#endif
    const Option<std::string> path = getenv(environmentVariable);
    return path.isSome() ? path.get() : std::string();
  }


  // Updates the value of LD_LIBRARY_PATH environment variable.
  inline void setPaths(const std::string& newPaths)
  {
    const char* environmentVariable =
#ifdef __APPLE__
      "DYLD_LIBRARY_PATH";
#else
      "LD_LIBRARY_PATH";
#endif
    os::setenv(environmentVariable, newPaths);
  }


  // Append newPath to the current value of LD_LIBRARY_PATH environment
  // variable.
  inline void appendPaths(const std::string& newPaths)
  {
    if (paths().empty()) {
      setPaths(newPaths);
    }
    else {
      setPaths(paths() + ":" + newPaths);
    }
  }

} // namespace libraries {

inline auto access(const std::string& fileName, int accessMode) ->
decltype(_access(fileName.c_str(), accessMode))
{
  return _access(fileName.c_str(), accessMode);
}
} // namespace os {


#endif // __STOUT_WINDOWS_OS_HPP__
