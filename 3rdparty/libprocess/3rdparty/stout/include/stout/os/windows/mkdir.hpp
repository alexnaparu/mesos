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

#ifndef __STOUT_OS_WINDOWS_MKDIR_HPP__
#define __STOUT_OS_WINDOWS_MKDIR_HPP__

#include <string>
#include <vector>

#include <stout/error.hpp>
#include <stout/nothing.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>
#include <stout/windows.hpp>

#include <stout/os/constants.hpp>

namespace os {

inline Try<Nothing> mkdir(const std::string& directory, bool recursive = true)
{
  if (!recursive) {
    if (::_mkdir(directory.c_str()) < 0) {
      return ErrnoError();
    }
  }
  else {
    std::string path = "";
    std::vector<std::string> tokens;

    // Absolute paths start with a backslash.
    bool absolute_path =
      (directory.find_first_of(os::DIRECTORY_SEPARATOR) == 0);

    // Get root (volume) of the path.
    char volume_name[MAX_PATH];
    BOOL result = ::GetVolumePathName(directory.c_str(), volume_name, MAX_PATH);
    if (!result) {
      // `GetVolumePath` failed, which means that the path could not be
      // resolved. If this is an absolute path, there's nothing more we can do.
      // If it'sa relative path, it's safe to attempt calling `mkdir`.
      if (absolute_path) {
        return Error("os::mkdir: Could not find path '" + directory + "'");
      }

      tokens = strings::tokenize(directory, os::DIRECTORY_SEPARATOR);
    }
    else {
      // The volume returned by `GetVolumePath` might be different than what the
      // `directory` argument specified. For instance, calling `GetVolumePath`
      // on a 'temp\dir1\dir2' will return the identifier of the current volume
      // (e.g. 'C:\').
      if (directory.find_first_of(volume_name) == 0) {
        // Skip the volume root when calling `mkdir` (as that would result in
        // an 'Access Denied' error.
        path = volume_name;

        // Tokenize the rest of the path
        const std::string relative_path = directory.substr(strlen(volume_name));
        tokens = strings::tokenize(relative_path, os::DIRECTORY_SEPARATOR);
      }
      else {
        // If `directory` is an absolute path that is missing a volume
        // specifier (such as '\temp\dir1'), we need to start at the root
        // rather than the current folder.
        if (absolute_path) {
          path = os::DIRECTORY_SEPARATOR;
        }

        // Tokenize the entire path
        tokens = strings::tokenize(directory, os::DIRECTORY_SEPARATOR);
      }
    }

    // Create each path component.
    foreach(const std::string& token, tokens) {
      path += token;
      if (::_mkdir(path.c_str()) < 0 && errno != EEXIST) {
        return ErrnoError();
      }

      path += os::DIRECTORY_SEPARATOR;
    }
  }

  return Nothing();
}

} // namespace os {

#endif // __STOUT_OS_WINDOWS_MKDIR_HPP__
