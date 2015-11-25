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
#ifndef __STOUT_WINDOWS_OS_GLOB_HPP__
#define __STOUT_WINDOWS_OS_GLOB_HPP__

#include <string>


namespace os {

// Returns the list of files that match the given (shell) pattern.
inline Try<std::list<std::string>> glob(const std::string& pattern)
{
  WIN32_FIND_DATA findData;
  const HANDLE searchHandle = FindFirstFile(pattern.c_str(), &findData);

  if (searchHandle == INVALID_HANDLE_VALUE) {
    return WindowsError(
      "`os::glob` failed when searching for files with pattern '" +
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

} // namespace os {

#endif // __STOUT_WINDOWS_OS_GLOB_HPP__
