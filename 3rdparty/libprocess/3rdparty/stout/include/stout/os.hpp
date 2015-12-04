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
#ifndef __STOUT_OS_HPP__
#define __STOUT_OS_HPP__

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <glog/logging.h>
#include <sys/types.h>

#include <list>
#include <queue>
#include <set>
#include <string>

#include <stout/error.hpp>
#include <stout/foreach.hpp>
#include <stout/none.hpp>
#include <stout/nothing.hpp>
#include <stout/option.hpp>
#include <stout/path.hpp>
#include <stout/strings.hpp>
#include <stout/try.hpp>
#include <stout/unreachable.hpp>
#include <stout/version.hpp>


// For readability, we minimize the number of #ifdef blocks in the code by
// splitting platform specifc system calls into separate directories.
#ifdef __WINDOWS__
#include <stout/windows/os.hpp>
#else
#include <stout/posix/os.hpp>
#endif // __WINDOWS__


namespace os {




} // namespace os {

#endif // __STOUT_OS_HPP__
