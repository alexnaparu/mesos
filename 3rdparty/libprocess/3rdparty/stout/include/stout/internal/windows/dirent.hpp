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
#ifndef __STOUT_INTERNAL_WINDOWS_DIRENT_HPP__
#define __STOUT_INTERNAL_WINDOWS_DIRENT_HPP__

#include <malloc.h>
#include <assert.h>

#include <stout/windows.hpp>


// Abbreviated version of the POSIX `dirent` struct. cf. specification[1].
//
// [1] http://www.gnu.org/software/libc/manual/html_node/Directory-Entries.html
struct dirent {
  char d_name[MAX_PATH];
  unsigned short d_namlen;
};


// `DIR` is normally an opaque struct in the standard, we expose the
// implementation here because this header is intended for internal use only.
struct DIR
{
  struct dirent curr;
  char *d_name;
  WIN32_FIND_DATA fd;
  HANDLE handle;
};


#ifdef __cplusplus
// Avoid the C++-style name-mangling linkage, and use C-style instead to give
// the appearance that this code is part of the real C standard library.
extern "C" {
#endif


void _freeDir(DIR* directory);
bool _openDirStream(DIR* directory);
bool _reentrantAdvanceDirStream(DIR* directory);


// Windows implementation of POSIX standard `opendir`. cf. specification[1].
// NOTE: this is for internal use only! It is marked as `inline` because we
// want to keep stout a header-only, and because we will only use this function
// a handful of times inside Stout.
//
// [1] http://www.gnu.org/software/libc/manual/html_node/Opening-a-Directory.html#Opening-a-Directory
inline DIR* opendir(const char* path)
{
  if (path == NULL) {
    errno = ENOTDIR;
    return NULL;
  }

  const size_t pathSize = strlen(path);

  if (pathSize == 0) {
    errno = ENOENT;
    return NULL;
  }

  const char windowsFolderSeparator = '\\';
  const char windowsDriveSeparator = ':';
  const char wildcard[] = "*";
  const char dirSeparatorAndWildcard[] = "\\*";

  // Allocate space for directory. Be sure to leave room at the end of
  // `directory->d_name` for a directory separator and a wildcard.
  DIR* directory = (DIR*) malloc(sizeof(DIR));

  if (!directory) {
    errno = ENOMEM;
    return NULL;
  }

  directory->d_name =
    (char*) malloc(pathSize + strlen(dirSeparatorAndWildcard));

  if (!directory->d_name) {
    errno = ENOMEM;
    free(directory);
    return NULL;
  }

  // Copy path over and append the appropriate postfix.
  strcpy(directory->d_name, path);

  const size_t lastCharInName =
    directory->d_name[strlen(directory->d_name) - 1];

  if (lastCharInName != windowsFolderSeparator &&
      lastCharInName != windowsDriveSeparator) {
    strcat(directory->d_name, dirSeparatorAndWildcard);
  } else {
    strcat(directory->d_name, wildcard);
  }

  if (!_openDirStream(directory)) {
    _freeDir(directory);
  }

  return directory;
}


// Implementation of the standard POSIX function. See documentation[1].
//
// On success: returns a pointer to the next directory entry, or `NULL` if
// we've reached the end of the stream.
//
// On failure: returns `NULL` and sets `errno`.
//
// NOTE: as with most POSIX implementations of this function, you must reset
// `errno` before calling `readdir`.
//
// [1] http://www.gnu.org/software/libc/manual/html_node/Reading_002fClosing-Directory.html#Reading_002fClosing-Directory
inline struct dirent* readdir(DIR* directory)
{
  if (directory == NULL) {
    errno = EBADF;
    return NULL;
  }

  if (!_reentrantAdvanceDirStream(directory)) {
    return NULL;
  }

  return &directory->curr;
}


// Implementation of the standard POSIX function. See documentation[1].
//
// On success: return 0 and set `*result` (note that `result` is not the same
// as `*result`) to point at the next directory entry, or `NULL` if we've
// reached the end of the stream.
//
// On failure: return a positive error number and set `*result` (not `result`)
// to point at `NULL`.
//
// [1] https://www.gnu.org/software/libc/manual/html_node/Reading_002fClosing-Directory.html
int readdir_r(DIR* directory, struct dirent* entry, struct dirent** result)
{
  if (directory == NULL) {
    errno = EBADF;
    *result = NULL;
    return 1;
  }

  if (!_reentrantAdvanceDirStream(directory)) {
    *result = NULL;
    return 0;
  }

  memcpy(entry, &directory->curr, sizeof(*entry));
  *result = &directory->curr;

  return 0;
}


// Implementation of the standard POSIX function. See documentation[1].
//
// On success, return 0; on failure, return -1 and set `errno` appropriately.
//
// [1] http://www.gnu.org/software/libc/manual/html_node/Reading_002fClosing-Directory.html#Reading_002fClosing-Directory
inline int closedir(DIR* directory)
{
  if (directory == NULL) {
    errno = EBADF;
    return -1;
  }

  BOOL searchClosed = false;

  if (directory->handle != INVALID_HANDLE_VALUE) {
    searchClosed = FindClose(directory->handle);
  }

  _freeDir(directory);

  return searchClosed ? 0 : -1;
}


inline void _freeDir(DIR* directory)
{
  assert(directory != NULL);

  free(directory);
}


inline bool _openDirStream(DIR* directory)
{
  assert(directory != NULL);

  directory->handle = FindFirstFile(directory->d_name, &directory->fd);

  if (directory->handle == INVALID_HANDLE_VALUE) {
    errno = ENOENT;
    return false;
  }

  strcpy(directory->curr.d_name, directory->fd.cFileName);
  directory->curr.d_namlen = strlen(directory->curr.d_name);

  return true;
}


inline bool _reentrantAdvanceDirStream(DIR* directory)
{
  assert(directory != NULL);

  if (!FindNextFile(directory->handle, &directory->fd)) {
    return false;
  }

  strcpy(directory->curr.d_name, directory->fd.cFileName);
  directory->curr.d_namlen = strlen(directory->curr.d_name);

  return true;
}


# ifdef __cplusplus
}
# endif


#endif // __STOUT_INTERNAL_WINDOWS_DIRENT_HPP__
