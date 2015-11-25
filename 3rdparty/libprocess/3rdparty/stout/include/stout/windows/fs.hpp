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


// Returns a list of all files matching the given pattern. This is meant to
// be a lightweight alternative to glob() - the only supported wildcards are
// `?` and `*`, and only when they appear at the tail end of `pattern` (e.g.
// `/root/dir/subdir/*.txt` or `/root/dir/subdir/file?.txt`
inline Try<std::list<std::string>> list(const std::string& pattern)
{
  WIN32_FIND_DATA findData;
  const HANDLE searchHandle = FindFirstFile(pattern.c_str(), &findData);

  if (searchHandle == INVALID_HANDLE_VALUE) {
    return WindowsError(
      "`fs::list` failed when searching for files with pattern '" +
      pattern + "'");
  }

  std::list<std::string> foundFiles;

  do {
    std::string currentFile(findData.cFileName);

    // Ignore `.` and `..` entries
    if (currentFile.compare(".") != 0 && currentFile.compare("..") != 0)
    {
      foundFiles.push_back(currentFile);
    }
  } while (FindNextFile(searchHandle, &findData));

  FindClose(searchHandle);
  return foundFiles;
}

} // namespace fs {

#endif // __STOUT_WINDOWS_FS_HPP__
