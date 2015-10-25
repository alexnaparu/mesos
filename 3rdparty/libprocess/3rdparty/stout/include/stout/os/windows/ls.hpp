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
#ifndef __STOUT_OS_WINDOWS_LS_HPP__
#define __STOUT_OS_WINDOWS_LS_HPP__

#include <list>
#include <string>

#include <stout/try.hpp>

#include <stout/os/stat.hpp>

#include <stout/internal/windows/dirent.hpp>


namespace os {

inline Try<std::list<std::string>> ls(const std::string& directory)
{
  DIR* dir = opendir(directory.c_str());

  if (dir == NULL) {
    return ErrnoError("Failed to opendir '" + directory + "'");
  }

  // NOTE: unlike the POSIX spec, our implementation of `dirent` is
  // constant-sized. In particular `dirent.d_name` is always of size
  // `MAX_PATH`, which is not guaranteed by the POSIX spec. Since we only need
  // to support our implementation of `dirent`, we can greatly simplify the
  // allocation logic here vs. the allocation logic in the POSIX-specific
  // version of `ls.hpp`.
  dirent* temp = (dirent*) malloc(sizeof(dirent));

  if (temp == NULL) {
    // Preserve malloc error.
    ErrnoError error("Failed to allocate directory entries");
    closedir(dir);
    return error;
  }

  std::list<std::string> result;
  struct dirent* entry;
  int error;

  while ((error = readdir_r(dir, temp, &entry)) == 0 && entry != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    result.push_back(entry->d_name);
  }

  free(temp);
  closedir(dir);

  if (error != 0) {
    errno = error;
    return ErrnoError("Failed to read directories");
  }

  return result;
}

} // namespace os {

#endif // __STOUT_OS_WINDOWS_LS_HPP__
