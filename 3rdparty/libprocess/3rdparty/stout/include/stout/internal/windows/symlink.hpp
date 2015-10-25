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
#ifndef __STOUT_INTERNAL_WINDOWS_SYMLINK_HPP__
#define __STOUT_INTERNAL_WINDOWS_SYMLINK_HPP__

#include <string>

#include <stout/try.hpp>
#include <stout/windows.hpp>

#include <stout/os/realpath.hpp>

#include <stout/internal/windows/reparsepoint.hpp>


namespace internal {
namespace windows {

// Gets symlink data for a given path, if it exists.
//
// This turns out to be a very complicated task on Windows. The gist of it is
// that we know that symlinks on Windows are implemented with the Reparse Point
// API, and so the process is a matter of:
//
//   1. Checking whether the attributes for the file/folder specified by the
//      path have the reparse point bit set; all symlinks are implemented with
//      reparse points, so this bit should be on all symlinks.
//   2. Opening a file/folder handle for that path, instructing it specifically
//      to open a handle for the symlink (if the path points at a symlink) and
//      *not* the file the symlink points at (as is the default). Note that
//      file and folder handles are different, so we have a function that
//      chooses appropriately.
//   3. Using `DeviceIoControl` to obtain information about the handle for this
//      reparse point, which we can then query to figure out if it's a reparse
//      point that is owned by the symlink filesysem filter driver.
//   4. If it is, then we report that this path does point at a symlink.
//
// NOTE: it may be helpful to consult the documentation for each of these
// functions, as they give you sources that justify the arguments to the
// obscure APIs we call to get this all working.
inline Try<SymbolicLink> querySymbolicLinkData(const std::string& path)
{
  // Convert to absolute path because Windows APIs expect it.
  const Result<std::string> absolutePath = os::realpath(path);

  if (!absolutePath.isSome()) {
    return Error(absolutePath.error());
  }

  // Windows has no built-in way to tell whether a path points at a symbolic
  // link; but, we know that symbolic links are implemented with reparse
  // points, so we begin by checking that.
  if (!reparsePointAttributeSet(absolutePath.get())) {
    return Error("Reparse point attribute is not set for path '" +
      absolutePath.get() + "', and therefore it is not a sybolic link.");
  }

  // Open `HANDLE` for the path; if it's a symlink, return a handle for the
  // symlink itself rather than the file or folder it points to. This function
  // intelligently chooses whether to open a folder handle or a file handle.
  const Try<HANDLE> handle = getHandleNoFollow(absolutePath.get());

  if (handle.isError()) {
    return Error(handle.error());
  }

  // Finally, retrieve symlink data for the handle, if any.
  Try<SymbolicLink> symlink = getSymbolicLinkData(handle.get());

  CloseHandle(handle.get());

  return symlink;
}

} // namespace windows {
} // namespace internal {

#endif // __STOUT_INTERNAL_WINDOWS_SYMLINK_HPP__
