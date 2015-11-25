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
#ifndef __STOUT_WINDOWS_FS_HPP__
#define __STOUT_WINDOWS_FS_HPP__

#include <string>

#include "bytes.hpp"
#include "error.hpp"
#include "nothing.hpp"
#include "try.hpp"

#include "stout/internal/windows/symlink.hpp"


namespace fs {

// Returns the total disk size in bytes.
inline Try<Bytes> size(const std::string& path = "/")
{
  ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
  if (!GetDiskFreeSpaceEx(path.c_str(), &freeBytesAvailable,
        &totalNumberOfBytes, &totalNumberOfFreeBytes))
  {
    return ErrnoError();
  }

  return Bytes(totalNumberOfFreeBytes.QuadPart);
}


// Returns relative disk usage of the file system that the given path
// is mounted at.
inline Try<double> usage(const std::string& path = "/")
{
  ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
  if (!GetDiskFreeSpaceEx(path.c_str(), &freeBytesAvailable,
        &totalNumberOfBytes, &totalNumberOfFreeBytes))
  {
    return ErrnoError("Error invoking GetDiskFreeSpaceEx on '" + path + "'");
  }

  return (double) (totalNumberOfBytes.QuadPart -
    totalNumberOfFreeBytes.QuadPart) / totalNumberOfBytes.QuadPart;
}


inline Try<Nothing> symlink(
    const std::string& original,
    const std::string& link)
{
  return internal::windows::createReparsePoint(link, original);
}

} // namespace fs {

#endif // __STOUT_WINDOWS_FS_HPP__
