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
#ifndef __STOUT_INTERNAL_WINDOWS_REPARSEPOINT_HPP__
#define __STOUT_INTERNAL_WINDOWS_REPARSEPOINT_HPP__

#include <string>

#include <stout/try.hpp>
#include <stout/windows.hpp>


namespace internal {
namespace windows {

// We pass this struct to `DeviceIoControl` to get information about a reparse
// point (including things like whether it's a symlink). It is normally part of
// the Device Driver Kit (DDK), specifically `nitfs.h`, but rather than taking
// a dependency on the DDK, we choose to simply copy the struct here. This is a
// well-worn path used by (e.g.) Boost FS[1], among others.
// [1] http://www.boost.org/doc/libs/1_46_1/libs/filesystem/v3/src/operations.cpp
typedef struct _REPARSE_DATA_BUFFER
{
  // Describes, among other things, which type of reparse point this is (e.g.,
  // a symlink).
  ULONG  ReparseTag;
  USHORT  ReparseDataLength;
  USHORT  Reserved;
  union
  {
    // Holds symlink data.
    struct
    {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG Flags;
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

#define REPARSE_MOUNTPOINT_HEADER_SIZE 8


// Convenience struct for holding symlink data.
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
  const DWORD attributes = GetFileAttributes(absolutePath.c_str());
  const bool reparseBitSet = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

  // Every flag is set if it's an invalid file or folder, so we check that
  // condition too.
  return attributes != INVALID_FILE_ATTRIBUTES && reparseBitSet;
}


// Attempts to extract symlink data out of a `REPARSE_DATA_BUFFER` (which could
// hold other things, e.g., mount point data).
inline Try<SymbolicLink> buildSymbolicLink(const REPARSE_DATA_BUFFER& data)
{
  const bool isSymLink = (data.ReparseTag & IO_REPARSE_TAG_SYMLINK) != 0;

  if (!isSymLink) {
    return Error("Data buffer is not a symlink");
  }

  const int targetNameStartIndex =
    data.SymbolicLinkReparseBuffer.SubstituteNameOffset / 2;
  const int targetNameLength =
    data.SymbolicLinkReparseBuffer.SubstituteNameLength / 2;
  const WCHAR* targetName =
    &data.SymbolicLinkReparseBuffer.PathBuffer[targetNameStartIndex];

  const int printNameStartIndex =
    data.SymbolicLinkReparseBuffer.PrintNameOffset / 2;
  const int printNameLength =
    data.SymbolicLinkReparseBuffer.PrintNameLength / 2;
  const WCHAR* displayName =
    &data.SymbolicLinkReparseBuffer.PathBuffer[printNameStartIndex];

  struct SymbolicLink symlink;
  symlink.substituteName.assign(targetName, targetName + targetNameLength);
  symlink.printName.assign(displayName, displayName + printNameLength);
  symlink.flags = data.SymbolicLinkReparseBuffer.Flags;

  return symlink;
}


// Attempts to get a file or folder handle for an absolute path, and does not
// follow symlinks. That is, if the path points at a symlink, the handle will
// refer to the symlink rather than the file or folder the symlink points at.
inline Try<HANDLE> getHandleNoFollow(const std::string& absolutePath)
{
  struct _stat s;
  bool resolvedPathIsDirectory = false;

  if (::_stat(absolutePath.c_str(), &s) >= 0) {
    resolvedPathIsDirectory = S_ISDIR(s.st_mode);
  }

  // NOTE: The `CreateFile` documentation[1] tells us which flags we need to
  // invoke to open a handle that will point at the symlink instead of the
  // folder or file it points at. The short answer is that you need to be sure
  // to pass in `OPEN_EXISTING` and `FILE_FLAG_OPEN_REPARSE_POINT` to get a
  // handle for the symlink and not the file the symlink points to.
  //
  // Note also that `CreateFile` will appropriately generate a handle for
  // either a folder of a file (they are different!), as long as you
  // appropriately set a magic flag, `FILE_FLAG_BACKUP_SEMANTICS`. It is not
  // clear why, or what this flag means, the documentation[1] only says it's
  // necessary.
  //
  // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/aa363858(v=vs.85).aspx
  const DWORD accessFlags = resolvedPathIsDirectory
    ? FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS
    : FILE_FLAG_OPEN_REPARSE_POINT;

  const HANDLE handle = CreateFile(
    absolutePath.c_str(),
    0,              // Ignored.
    0,              // Ignored.
    NULL,           // Ignored.
    OPEN_EXISTING,  // Open existing symlink.
    accessFlags,    // Open symlink, not the file it points to.
    NULL);          // Ignored.

  if (handle == INVALID_HANDLE_VALUE) {
    return Error("TODO");
  }

  return handle;
}


// Attempts to get the symlink data for a file or folder handle.
inline Try<SymbolicLink> getSymbolicLinkData(const HANDLE& handle)
{
  // To get the symlink data, we call `DeviceIoControl`. This function is part
  // of the Device Driver Kit (DDK), and buried away in a corner of the
  // DDK documentation[1] is an incomplete explanation of how to twist API to
  // get it to emit information about reparse points (and, thus, symlinks,
  // since symlinks are implemented with reparse points). This technique is a
  // hack, but it is used pretty much everywhere, including the Boost FS code,
  // though it's worth noting that they seem to use it incorrectly[2].
  //
  // Summarized, the documentation tells us that we need to pass in the magic
  // flag `FSCTL_GET_REPARSE_POINT` to get the function to populate a
  // `REPARSE_DATA_BUFFER` struct with data about a reparse point. What it
  // doesn't tell you is that this struct is in a header in the DDK, so in
  // order to get information about the reparse point, you must either take a
  // dependency on the DDK, or copy the struct from the header into your code.
  // We take a cue from Boost FS, and copy the struct into this header (see
  // above).
  //
  // Finally, for context, it may be worth looking at the (sparse)
  // documentation for `DeviceIoControl` itself.
  //
  // [1] https://msdn.microsoft.com/en-us/library/windows/desktop/aa364571(v=vs.85).aspx
  // [2] https://svn.boost.org/trac/boost/ticket/4663
  // [3] https://msdn.microsoft.com/en-us/library/windows/desktop/aa363216(v=vs.85).aspx

  REPARSE_DATA_BUFFER* reparsePointData =
    (REPARSE_DATA_BUFFER*)malloc(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
  const size_t reparsePointDataSize = MAXIMUM_REPARSE_DATA_BUFFER_SIZE;
  const DWORD junk = 0;

  // The semantics of this function are: get the reparse data associated with
  // the `handle` of some open directory or file, and that data in
  // `reparsePointData`.
  const BOOL reparseDataObtained = DeviceIoControl(
    handle,                  // handle to file or directory
    FSCTL_GET_REPARSE_POINT, // Gets reparse point data for file/folder handle.
    NULL,                    // Ignored.
    0,                       // Ignored.
    (LPVOID)reparsePointData,
    reparsePointDataSize,
    (LPDWORD)&junk,          // Ignored.
    NULL);                   // Ignored.

  if (!reparseDataObtained) {
    free(reparsePointData);
    return Error("TODO");
  }

  Try<SymbolicLink> symlink = buildSymbolicLink(*reparsePointData);
  free(reparsePointData);
  return symlink;
}

} // namespace windows {
} // namespace internal {

#endif // __STOUT_INTERNAL_WINDOWS_REPARSEPOINT_HPP__
