/**
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License
*/

#ifndef __PROCESS_POSIX_FUTURE_HPP__
#define __PROCESS_POSIX_FUTURE_HPP__


// Future<T>::onReady templates
#define PROCESS_FUTURE_ONREADY_F_LESSPREFER_TEMPLATE template <typename F, typename = typename std::result_of<F()>::type> // NOLINT(whitespace/line_length)

// Future<T>::onFailed template.
#define PROCESS_FUTURE_ONFAILED_F_LESSPREFER_TEMPLATE template <typename F, typename = typename std::result_of<F()>::type> // NOLINT(whitespace/line_length)

// Future<T>::onAny template.
#define PROCESS_FUTURE_ONANY_F_LESSPREFER_TEMPLATE template <typename F, typename = typename std::result_of<F()>::type> // NOLINT(whitespace/line_length)

// Future<T>::then templates.
#define PROCESS_FUTURE_THEN_DEFERRED_PREFER_TEMPLATE template <typename F, typename X = typename internal::unwrap<typename std::result_of<F(const T&)>::type>::type> // NOLINT(whitespace/line_length)

#define PROCESS_FUTURE_THEN_DEFERRED_LESSPREFER_TEMPLATE template <typename F, typename X = typename internal::unwrap<typename std::result_of<F()>::type>::type> // NOLINT(whitespace/line_length)

#define PROCESS_FUTURE_THEN_F_PREFER_TEMPLATE template <typename F, typename X = typename internal::unwrap<typename std::result_of<F(const T&)>::type>::type> // NOLINT(whitespace/line_length)

#define PROCESS_FUTURE_THEN_F_LESSPREFER_TEMPLATE template <typename F, typename X = typename internal::unwrap<typename std::result_of<F()>::type>::type> // NOLINT(whitespace/line_length)

#endif // __PROCESS_POSIX_FUTURE_HPP__
