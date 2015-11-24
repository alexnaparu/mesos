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

#ifndef __STOUT_INTERNAL_WINDOWS_REPARSEPOINT_HPP__
#define __STOUT_INTERNAL_WINDOWS_REPARSEPOINT_HPP__

#include <mutex>
#include <string>

#include <stout/nothing.hpp>
#include <stout/try.hpp>
#include <stout/windows.hpp>

#include <stout/os/mkdir.hpp>
#include <stout/os/realpath.hpp>

// We pass this struct to `DeviceIoControl` to get information about a reparse
// point (including things like whether it's a symlink). It is normally part of
// the Device Driver Kit (DDK), specifically `nitfs.h`, but rather than taking
// a dependency on the DDK, we choose to simply copy the struct here. This is a
// well-worn path used by (e.g.) Boost FS[1], among others. See documentation
// here[2][3].
//
// [1] http://www.boost.org/doc/libs/1_46_1/libs/filesystem/v3/src/operations.cpp
// [2] https://msdn.microsoft.com/en-us/library/cc232007.aspx
// [3] https://msdn.microsoft.com/en-us/library/cc232005.aspx
//
// We are declaring this structure (and the REPARSE_DATA_BUFFER_HEADER_SIZE
// macro right below it in the global namespace, to be consistent with the
// original Windows DDK declarations.
typedef struct _REPARSE_DATA_BUFFER
{
  // Describes, among other things, which type of reparse point this is (e.g.,
  // a symlink).
  ULONG  ReparseTag;
  // Size in bytes of common portion of the `REPARSE_DATA_BUFFER`.
  USHORT  ReparseDataLength;
  // Unused. Ignore.
  USHORT  Reserved;
  union
  {
    // Holds symlink data.
    struct
    {
      // Byte offset in `PathBuffer` where the substitute name begins.
      // Calculated as an offset from 0.
      USHORT SubstituteNameOffset;
      // Length in bytes of the substitute name.
      USHORT SubstituteNameLength;
      // Byte offset in `PathBuffer` where the print name begins. Calculated as
      // an offset from 0.
      USHORT PrintNameOffset;
      // Length in bytes of the print name.
      USHORT PrintNameLength;
      // Indicates whether symlink is absolute or relative. If flags containing
      // `SYMLINK_FLAG_RELATIVE`, then the substitute name is a relative
      // symlink.
      ULONG Flags;
      // The first byte of the path string -- according to the documentation[1],
      // this is followed in memory by the rest of the path string. The "path
      // string" itself is a unicode char array containing both substitute name
      // and print name. They can occur in any order. Use the offset and length
      // of each in this struct to calculate where each starts and ends.
      //
      // [1] https://msdn.microsoft.com/en-us/library/windows/hardware/ff552012(v=vs.85).aspx
      WCHAR PathBuffer[1];
    } SymbolicLinkReparseBuffer;

    // Unused: holds mount point data.
    struct
    {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR PathBuffer[1];
    } MountPointReparseBuffer;
    struct
    {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  };
} REPARSE_DATA_BUFFER;

#define REPARSE_DATA_BUFFER_HEADER_SIZE \
  FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)

namespace internal {
namespace windows {

// Convenience struct for holding symlink data, meant purely for internal use.
// We pass this around instead of the `REPARSE_DATA_BUFFER` struct, simply
// because this struct is easier to deal with and reason about.
struct SymbolicLink
{
  std::wstring substituteName;
  std::wstring printName;
  ULONG flags;
};


// Checks file/folder attributes for a path to see if the reparse point
// attribute is set; this indicates whether the path points at a reparse point,
// rather than a "normal" file or folder.
inline bool reparsePointAttributeSet(const std::string& absolutePath)
{
  const DWORD attributes = ::GetFileAttributes(absolutePath.c_str());

  if (attributes == INVALID_FILE_ATTRIBUTES) {
      return false;
  }

  const bool reparseBitSet = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

  return reparseBitSet;
}


// Attempts to extract symlink data out of a `REPARSE_DATA_BUFFER` (which could
// hold other things, e.g., mount point data).
inline Try<SymbolicLink> buildSymbolicLink(
  const std::shared_ptr<REPARSE_DATA_BUFFER> data)
{
  const bool isSymLink = (data->ReparseTag & IO_REPARSE_TAG_SYMLINK) != 0;

  if (!isSymLink) {
    return Error("Data buffer is not a symlink");
  }

  const int targetNameStartIndex =
    data->SymbolicLinkReparseBuffer.SubstituteNameOffset / sizeof(WCHAR);
  const int targetNameLength =
    data->SymbolicLinkReparseBuffer.SubstituteNameLength / sizeof(WCHAR);
  const WCHAR* targetName =
    data->SymbolicLinkReparseBuffer.PathBuffer + targetNameStartIndex;

  const int printNameStartIndex =
    data->SymbolicLinkReparseBuffer.PrintNameOffset / sizeof(WCHAR);
  const int printNameLength =
    data->SymbolicLinkReparseBuffer.PrintNameLength / sizeof(WCHAR);
  const WCHAR* displayName =
    data->SymbolicLinkReparseBuffer.PathBuffer + printNameStartIndex;

  struct SymbolicLink symlink;
  symlink.substituteName.assign(targetName, targetName + targetNameLength);
  symlink.printName.assign(displayName, displayName + printNameLength);
  symlink.flags = data->SymbolicLinkReparseBuffer.Flags;

  return symlink;
}


// Attempts to get a file or folder handle for an absolute path, and does not
// follow symlinks. That is, if the path points at a symlink, the handle will
// refer to the symlink rather than the file or folder the symlink points at.
inline Try<shared_handle> getHandleNoFollow(const std::string& absolutePath)
{
  struct _stat s;
  bool resolvedPathIsDirectory = false;

  if (::_stat(absolutePath.c_str(), &s) == 0) {
    resolvedPathIsDirectory = S_ISDIR(s.st_mode);
  }
  else
  {
    return ErrnoError("`_stat` failed on path '" + absolutePath + "'");
  }

  // NOTE: According to the `CreateFile` documentation[1], the `OPEN_EXISTING`
  // and `FILE_FLAG_OPEN_REPARSE_POINT` flags need to be used when getting a
  // handle for the symlink.
  //
  // Note also that `CreateFile` will appropriately generate a handle for
  // either a folder or a file, as long as the appropriate flag is being set:
  // `FILE_FLAG_BACKUP_SEMANTICS` or `FILE_FLAG_OPEN_REPARSE_POINT`.
  //
  // The `FILE_FLAG_BACKUP_SEMANTICS` flag is being set whenever the target is
  // a directory. According to MSDN[1]: "You must set this flag to obtain a
  // handle to a directory. A directory handle can be passed to some functions
  // instead of a file handle". More `FILE_FLAG_BACKUP_SEMANTICS` documentation
  // can be found in MSDN[2].
  //
  // The `GENERIC_READ` flag is being used because it's the most common way of
  // opening a file for reading only. The `FILE_SHARE_READ` allows other
  // processes to read the file at the same time. MSDN[1] provides a more
  // detailed explanation of these flags.
  //
  // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/aa363858(v=vs.85).aspx
  // [2] https://msdn.microsoft.com/en-us/library/windows/desktop/aa364399(v=vs.85).aspx
  const DWORD accessFlags = resolvedPathIsDirectory
    ? (FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS)
    : FILE_FLAG_OPEN_REPARSE_POINT;

  const HANDLE handle = CreateFile(
    absolutePath.c_str(),
    GENERIC_READ,     // Open the file for reading only.
    FILE_SHARE_READ,  // Just reading this file, allow others to do the same.
    NULL,             // Ignored.
    OPEN_EXISTING,    // Open existing symlink.
    accessFlags,      // Open symlink, not the file it points to.
    NULL);            // Ignored.

  if (handle == INVALID_HANDLE_VALUE) {
    return WindowsError("internal::windows::getHandleNoFollow`: "
      "`CreateFile` call failed");
  }

  return shared_handle(handle, CloseHandle);
}


// Attempts to get the symlink data for a file or folder handle.
inline Try<SymbolicLink> getSymbolicLinkData(const shared_handle handle)
{
  // To get the symlink data, we call `DeviceIoControl`. This function is part
  // of the Device Driver Kit (DDK)[1] and, along with `FSCTL_GET_REPARSE_POINT`
  // is used to emit information about reparse points (and, thus, symlinks,
  // since symlinks are implemented with reparse points). This technique is
  // being used in Boost FS code as well[2].
  //
  // Summarized, the documentation tells us that we need to pass in
  // `FSCTL_GET_REPARSE_POINT` to get the function to populate a
  // `REPARSE_DATA_BUFFER` struct with data about a reparse point.
  // The `REPARSE_DATA_BUFFER` struct is defined in a DDK header file,
  // so to avoid bringing in a multitude of DDK headers we take a cue from
  // Boost FS, and copy the struct into this header (see above).
  //
  // Finally, for context, it may be worth looking at the MSDN
  // documentation[3] for `DeviceIoControl` itself.
  //
  // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/aa364571(v=vs.85).aspx
  // [2] https://svn.boost.org/trac/boost/ticket/4663
  // [3] https://msdn.microsoft.com/en-us/library/windows/desktop/aa363216(v=vs.85).aspx
  const size_t reparsePointDataSize = MAXIMUM_REPARSE_DATA_BUFFER_SIZE;
  std::shared_ptr<REPARSE_DATA_BUFFER> reparsePointData(
    (REPARSE_DATA_BUFFER*)(new BYTE[reparsePointDataSize]));
  const DWORD ignored = 0;

  // The semantics of this function are: get the reparse data associated with
  // the `handle` of some open directory or file, and that data in
  // `reparsePointData`.
  const BOOL reparseDataObtained = DeviceIoControl(
      handle.get(),             // Handle to file or directory.
      FSCTL_GET_REPARSE_POINT,  // Gets reparse point data for file/folder.
      NULL,                     // Ignored.
      0,                        // Ignored.
      reparsePointData.get(),
      reparsePointDataSize,
      (LPDWORD)&ignored,        // Ignored.
      NULL);                    // Ignored.

  if (!reparseDataObtained) {
    return WindowsError(
        "internal::windows::getSymbolicLinkData`: `DeviceIoControl` call "
        "failed");
  }

  Try<SymbolicLink> symlink = buildSymbolicLink(reparsePointData);
  return symlink;
}


// Creates a reparse point with the specified target. The target can be either
// a file (in which case a junction is created), or a folder (in which case a
// mount point is created).
//
// Calling this function results in a temporary elevation of the process token's
// privileges (if those privileges are not already held), to allow for junction
// or mount point creation. This operation is gated by a static mutex, which
// makes it thread-safe.
inline Try<Nothing> createSymbolicLink(
  const std::string& reparsePoint,
  const std::string& target)
{
  // Normalize input paths.
  const Result<std::string> realReparsePointPath = os::realpath(reparsePoint);
  const Result<std::string> realTargetPath = os::realpath(target);

  if (!realReparsePointPath.isSome()) {
    return Error(realReparsePointPath.error());
  }

  if (!realTargetPath.isSome()) {
    return Error(realTargetPath.error());
  }

  const std::string& absoluteReparsePointPath(realReparsePointPath.get());
  const std::string& absoluteTargetPath(realTargetPath.get());

  // Determine if target is a folder or a file. This makes a difference
  // in the way we call `CreateSymbolicLink`.
  struct _stat s;
  if (::_stat(absoluteTargetPath.c_str(), &s) != 0) {
    return ErrnoError("`_stat` failed on path '" + absoluteTargetPath + "'");
  }

  const bool targetIsFolder = S_ISDIR(s.st_mode);

  // Bail out if target is already a reparse point.
  if (reparsePointAttributeSet(absoluteTargetPath)) {
    return Error(
      "Path '" + absoluteTargetPath + "' is already a reparse point");
  }

  // `CreateSymbolicLink` adjusts the process token's privileges to allow for
  // symlink creation. MSDN[1] makes no guarantee when it comes to the thread
  // safety of this operation, so we are making use of a mutex to prevent
  // multiple concurrent calls.
  //
  // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/aa363866(v=vs.85).aspx
  static std::mutex adjustPrivilegesMutex;
  std::lock_guard<std::mutex> lock(adjustPrivilegesMutex);

  if (!::CreateSymbolicLink(
      reparsePoint.c_str(),  // path to the symbolic link
      target.c_str(),        // symlink target
      targetIsFolder ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0)) // as described in [1]
  {
    return WindowsError(
        "`internal::windows::createSymbolicLink`: `CreateSymbolicLink` "
        "call failed");
  }

  return Nothing();
}

} // namespace windows {
} // namespace internal {

#endif // __STOUT_INTERNAL_WINDOWS_REPARSEPOINT_HPP__
