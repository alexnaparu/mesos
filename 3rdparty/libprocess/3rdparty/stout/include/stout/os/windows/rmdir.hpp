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

#ifndef __STOUT_OS_WINDOWS_RMDIR_HPP__
#define __STOUT_OS_WINDOWS_RMDIR_HPP__

#include <stout/nothing.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>
#include <stout/windows.hpp>

#include <stout/os/realpath.hpp>
#include <stout/os/stat.hpp>

#include <stout/windows/error.hpp>


namespace os {
namespace internal {

// Recursive version of `RemoveDirectory`. NOTE: unlike `rmdir`, this requires
// Windows-formatted paths, and therefore should be in the `internal` namespace.
Try<Nothing> recursiveRemoveDirectory(const std::string& path)
{
  // Appending a slash here if the path doesn't already have one simplifies
  // path join logic later, because (unlike Unix) Windows doesn't like double
  // slashes in paths.
  std::string currentPath;

  if (!strings::endsWith(path, "\\")) {
    currentPath = path + "\\";
  } else {
    currentPath = path;
  }

  // Get first file matching pattern `X:\path\to\wherever\*`.
  WIN32_FIND_DATA found;
  const std::string searchPattern = currentPath + "*";
  const shared_handle searchHandle(
      FindFirstFile(searchPattern.c_str(), &found),
      FindClose);

  if (searchHandle.get() == INVALID_HANDLE_VALUE) {
    return WindowsError(
        "`os::internal::recursiveRemoveDirectory` failed when searching "
        "for files with pattern '" + searchPattern + "'");
  }

  do {
    // NOTE: do-while is appropriate here because folder is guaranteed to have
    // at least a file called `.` (and probably also one called `..`).
    const std::string currentFile(found.cFileName);

    const bool isCurrentDirectory = currentFile.compare(".") == 0;
    const bool isParentDirectory = currentFile.compare("..") == 0;

    // Don't try to delete `.` and `..` files in directory.
    if (isCurrentDirectory || isParentDirectory) {
      continue;
    }

    // Path to remove.
    const std::string currentAbsolutePath = currentPath + currentFile;

    const bool isDirectory = os::stat::isdir(currentAbsolutePath);

    // Delete current path, whether it's a directory, file, or symlink.
    if (isDirectory) {
      Try<Nothing> removed = recursiveRemoveDirectory(currentAbsolutePath);

      if (removed.isError()) {
        return Error(removed.error());
      }
    } else {
      // NOTE: this also handles symbolic links.
      if (::remove(currentAbsolutePath.c_str()) != 0) {
        return WindowsError(
            "`os::internal::recursiveRemoveDirectory` attempted to delete "
            "file '" + currentAbsolutePath + "', but failed");
      }
    }
  } while (FindNextFile(searchHandle.get(), &found));

  // Finally, remove current directory.
  if (::_rmdir(currentPath.c_str()) == -1) {
    return ErrnoError(
        "`os::internal::recursiveRemoveDirectory` attempted to delete file '" +
        currentPath + "', but failed");
  }

  return Nothing();
}

} // namespace internal {


// By default, recursively deletes a directory akin to: 'rm -r'. If the
// programmer sets recursive to false, it deletes a directory akin to: 'rmdir'.
// Note that this function expects an absolute path.
inline Try<Nothing> rmdir(const std::string& directory, bool recursive = true)
{
  // Canonicalize the path to Windows style for the call to
  // `recursiveRemoveDirectory`.
  Result<std::string> root = os::realpath(directory);

  if (root.isError()) {
    return Error(root.error());
  } else if (root.isNone()) {
    return Error(
        "Argument to `os::rmdir` is not a valid directory or file: '" +
        directory + "'");
  }

  if (!recursive) {
    if (::_rmdir(directory.c_str()) < 0) {
      return ErrnoError();
    } else {
      return Nothing();
    }
  } else {
    return os::internal::recursiveRemoveDirectory(root.get());
  }
}

} // namespace os {


#endif // __STOUT_OS_WINDOWS_RMDIR_HPP__
