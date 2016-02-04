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

#ifndef __STOUT_OS_WINDOWS_KILL_HPP__
#define __STOUT_OS_WINDOWS_KILL_HPP__

#include <stout/windows.hpp>
#include <TlHelp32.h>

#define KILL_PASS 0
#define KILL_FAIL -1

namespace os {

  inline int SuspendResumeProcess(pid_t pid, int sig)
  {
    // To suspend a process, we have to suspend all threads
    // in this process.

    // Make sure sig values could only be SIGSTOP or SIGCONT.
    if (sig != SIGSTOP && sig != SIGCONT) {
      return KILL_FAIL;
    }

    // Get a snapshot of the threads in the system.
    HANDLE threads_handle = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, pid);
    if (threads_handle == INVALID_HANDLE_VALUE || threads_handle == NULL) {
      return KILL_FAIL;
    }

    THREADENTRY32 thread_entry;
    thread_entry.dwSize = sizeof(THREADENTRY32);

    // Point to the first thread and start loop.
    Thread32First(threads_handle, &thread_entry);
    do {
      HANDLE thread_handle = NULL;

      // If current thread is part of the process;
      // apply action based on passed in signal.
      if (thread_entry.th32OwnerProcessID == pid) {
        thread_handle = OpenThread(
          THREAD_ALL_ACCESS,
          false,
          thread_entry.th32ThreadID);

        // We will go with the assumption: if thread handle is not available
        // then we can just continue.
        if (thread_handle == INVALID_HANDLE_VALUE || thread_handle == NULL) {
          continue;
        }

        // Suspend the thread.
        if (sig == SIGSTOP) {
          SuspendThread(thread_handle);
        }
        // Resume the thread.
        else if (sig == SIGCONT) {
          ResumeThread(thread_handle);
        }

        // Clean up for this iteration.
        CloseHandle(thread_handle);
        thread_handle = NULL;
      }
    } while (Thread32Next(threads_handle, &thread_entry));

    // Clean-up.
    CloseHandle(threads_handle);
    threads_handle = NULL;

    return KILL_PASS;
  }


  inline int TerminateProcess(pid_t pid)
  {
    HANDLE process_handle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (process_handle == INVALID_HANDLE_VALUE || process_handle == NULL) {
      return KILL_FAIL;
    }

    BOOL result = ::TerminateProcess(process_handle, 1);
    if (!result) {
      return KILL_FAIL;
    }

    return KILL_PASS;
  }


  inline int kill(pid_t pid, int sig)
  {
    // If sig is SIGSTOP or SIGCONT
    // call SuspendResumeProcess.
    // If sig is SIGKILL call TerminateProcess
    // otherwise return -1 (fail).
    if (sig == SIGSTOP || sig == SIGCONT) {
      return SuspendResumeProcess(pid, sig);
    }
    else if (sig == SIGKILL) {
      return TerminateProcess(pid);
    }

    return KILL_FAIL;
  }

} // namespace os {

#endif // __STOUT_OS_WINDOWS_KILL_HPP__
