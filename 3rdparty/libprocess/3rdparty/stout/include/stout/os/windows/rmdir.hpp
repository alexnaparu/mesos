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
#ifndef __STOUT_OS_WINDOWS_RMDIR_HPP__
#define __STOUT_OS_WINDOWS_RMDIR_HPP__

#include <stout/nothing.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>
#include <stout/windows.hpp>

#include <stout/os/realpath.hpp>

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
  const HANDLE searchHandle = FindFirstFile(searchPattern.c_str(), &found);

  if (searchHandle == INVALID_HANDLE_VALUE) {
    FindClose(searchHandle);
    return WindowsError(
      "`os::rmdir` failed when searching for files with pattern '" +
      searchPattern + "'");
  }

  do {
    // NOTE: do-while is appropriate here because folder guaranteed at least to
    // have a file called `.` (and probably also one called `..`).
    const std::string currentFile(found.cFileName);

    const bool isCurrentDirectory = currentFile.compare(".") == 0;
    const bool isPreviousDirectory = currentFile.compare("..") == 0;

    // Don't try to delete `.` and `..` files in directory.
    if (isCurrentDirectory || isPreviousDirectory) {
      continue;
    }

    // Path to remove.
    const std::string currentAbsolutePath = currentPath + currentFile;

    const DWORD attributes = GetFileAttributes(currentAbsolutePath.c_str());
    const bool isDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (attributes == INVALID_FILE_ATTRIBUTES) {
      FindClose(searchHandle);
      return WindowsError(
        "`os::rmdir` attempted to delete path '" + currentAbsolutePath +
        "', but found it was invalid");
    }

    // Delete current path, whether it's a directory, file, or symlink.
    if (isDirectory) {
      Try<Nothing> removed = recursiveRemoveDirectory(currentAbsolutePath);

      if (removed.isError()) {
        FindClose(searchHandle);
        return WindowsError(
          "`os::rmdir` attempted to delete directory '" + currentAbsolutePath +
          "', but failed");
      }
    }
    else
    {
      // NOTE: this also handles symbolic links.
      if (::remove(currentAbsolutePath.c_str()) != 0) {
        FindClose(searchHandle);
        return WindowsError(
          "`os::rmdir` attempted to delete file '" + currentAbsolutePath +
          "', but failed");
      }
    }
  } while (FindNextFile(searchHandle, &found));

  // Finally, remove current directory.
  FindClose(searchHandle);
  if (::rmdir(currentPath.c_str()) == -1) {
    return ErrnoError(
      "`os::rmdir` attempted to delete file '" + currentPath +
      "', but failed");
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
    if (::rmdir(directory.c_str()) < 0) {
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
