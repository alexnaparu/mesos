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

#ifndef __STOUT_ERROR_BASE_HPP__
#define __STOUT_ERROR_BASE_HPP__

#include <errno.h>

#include <string>

#include <stout/os/strerror.hpp>

// A useful type that can be used to represent a Try that has
// failed. You can also use 'ErrnoError' to append the error message
// associated with the current 'errno' to your own error message.
//
// Examples:
//
//   Result<int> result = Error("uninitialized");
//   Try<std::string> = Error("uninitialized");
//
//   void foo(Try<std::string> t) {}
//
//   foo(Error("some error here"));

class Error
{
public:
  template<typename... T> Error(const std::string& format, T&&... args) {
    // Determine how many characters are needed to store the expanded
    // string.
    const int size = ::snprintf(NULL, 0, format.c_str(), args...);
    if (size > 0) {
      // Allocate sufficient space for the expanded string.
      message.reserve(size);
      ::snprintf((char *)message.data(), size + 1, format.c_str(), args...);
    }
    else {
      message.assign(
          "`Error::Error()` - incorrect string format '" + format + "'");
      }
  }

  std::string message;
};


class ErrnoError : public Error
{
public:
  ErrnoError() : Error(os::strerror(errno)) {}

  ErrnoError(const std::string& message)
    : Error(message + ": " + os::strerror(errno)) {}
};

// Creates a formatted error message prefixed with the calling function's name
// (e.g. "`foo`: ...").
// The _TYPE argument specifies the error class to be used. Subsequent
// arguments determine the format of the error message (printf-style).
//
// Example:
//
// Try<Nothing> bar() {
//   return _ERROR(WindowsError, "Could not open file '%s', error code %d", \
          "file1.txt", -1);
// }
//
// will generate an error with the message:
//
// `bar`: Could not open file 'file1.txt', error code -1
#define _ERROR(_TYPE, _FORMAT, ...) \
  _TYPE("`%s`: "_FORMAT, __func__, __VA_ARGS__)

#endif // __STOUT_ERROR_BASE_HPP__
