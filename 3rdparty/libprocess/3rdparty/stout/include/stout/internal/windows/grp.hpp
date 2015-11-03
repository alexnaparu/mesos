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
#ifndef __STOUT_INTERNAL_WINDOWS_GRP_HPP__
#define __STOUT_INTERNAL_WINDOWS_GRP_HPP__

#include <sys/types.h>

#include <stout/windows.hpp>


// Dummy struct for POSIX compliance.
struct group
{
  char* gr_name; // The name of the group.
  gid_t gr_gid;  // Numerical group ID.
  char** gr_mem; // Pointer to a null-terminated array of character pointers to
                 // member names.
};


extern "C"
{
// Dummy implementation of `getgrgid` for POSIX compliance.
struct group* getgrgid(gid_t)
{
  return NULL;
}
}


#endif // __STOUT_INTERNAL_WINDOWS_GRP_HPP__
