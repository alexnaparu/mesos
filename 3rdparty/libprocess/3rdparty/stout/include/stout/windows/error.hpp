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
#ifndef __STOUT_WINDOWS_ERROR_HPP__
#define __STOUT_WINDOWS_ERROR_HPP__

#include <stout/error.hpp>
#include <stout/windows.hpp>


// A useful type that can be used to represent a Try that has failed. This is a
// lot like `ErrnoError`, except instead of wrapping an error coming from the C
// standard libraries, it wraps an error coming from the Windows APIs.
class WindowsError : public Error
{
public:
  WindowsError()
    : Error(GetLastErrorAsString()) {}

  WindowsError(const std::string& message)
    : Error(message + ": " + GetLastErrorAsString()) {}

private:
  static std::string GetLastErrorAsString()
  {
    DWORD errorCode = ::GetLastError();

    // Default if no error.
    if (errorCode == 0) {
      return std::string();
    }

    DWORD allocMessageBufferFlags =
      FORMAT_MESSAGE_ALLOCATE_BUFFER |
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD defaultLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);

    // This following function `FormatMessage` is a lot like `strerror`, except
    // it pretty-prints errors from the Windows API instead of from the C
    // standard library. Basically, the semantics are: we pass in `errorCode`,
    // and it allocates room for a pretty-printed error message at
    // `messageBuffer`, and then dumps said pretty-printed error message at
    // that address, in our `defaultLanguage`.
    //
    // The 5th actual parameter (namely `(LPSTR)&messageBuffer`), may look
    // strange to readers of this code. It is copied directly out of the
    // documentation[1], and is unfortunately required to get the
    // pretty-printed error message. The short story is:
    //
    //   * The flag `FORMAT_MESSAGE_ALLOCATE_BUFFER` tells `FormatMessage` to
    //     allocate space for the error message at `messageBuffer`'s address on
    //     our behalf.
    //   * But, `messageBuffer` is of type `LPSTR` a.k.a. `char*`.
    //   * So, to solve this problem, the API writers decided that when you
    //     pass that flag in, `FormatMessage` will treat the 5th parameter not
    //     as `LPSTR` (which is what the type is in the function signagure),
    //     but as `LPSTR*` a.k.a. `char**`, which (assuming you've casted the
    //     parameter correctly) allows it to allocate the message on your
    //     behalf, and dump it at the address of (in our case) `messageBuffer`.
    //   * This is why we need this strange cast that you see below.
    //
    // Finally, and this is important: it is up to the user to free the memory
    // with `LocalFree`! The line below where we do this comes directly from
    // the documentation as well.
    //
    // As far as I can tell there is no simpler version of this function.
    //
    // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/ms679351(v=vs.85).aspx
    LPSTR messageBuffer;
    size_t size = FormatMessage(
      allocMessageBufferFlags,
      NULL,                  // Ignored.
      errorCode,
      defaultLanguage,
      (LPSTR)&messageBuffer, // See comment above for note about quirky cast.
      0,                     // Ignored.
      NULL);                 // Ignored.

    std::string message(messageBuffer, size);

    // Required per documentation above.
    LocalFree(messageBuffer);

    return message;
  }
};

#endif // __STOUT_WINDOWS_ERROR_HPP__
