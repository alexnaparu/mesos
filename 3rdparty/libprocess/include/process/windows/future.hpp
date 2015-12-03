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

#ifndef __PROCESS_WINDOWS_FUTURE_HPP__
#define __PROCESS_WINDOWS_FUTURE_HPP__


// MSVC is lacking a few capabilities requiring the following template
// declarations as a work-around. In particular, we talked to Stephan T.
// Lavavej, who made the following recommendations:
//
//   1. VS 2015 won't support C++14 result_of SFINAE until Update 2,  so
//      result_of must be replaced with decltype(invoke).
//   2. VS 2015 won't support C++14 std::function SFINAE until Update 2, so
//      converting _Deferred to std::function must be done by explicitly
//      calling _Deferred's conversion function.
//   3. The Standard (C++11 through 17) does not require bind's function call
//      operator to SFINAE, and VS 2015's doesn't.  is_bind_expression can be
//      used to manually reroute bind expressions to the 1-arg overload, where
//      (conveniently) the argument will be ignored if necessary.

// Future<T>::onReady template.
// template <typename F, typename = typename std::result_of<F()>::type>
#define PROCESS_FUTURE_ONREADY_F_LESSPREFER_TEMPLATE template <typename F, typename = decltype(std::declval<enable_if_t<!is_bind_expression<decay_t<F>>::value, F>>()())>  // NOLINT(whitespace/line_length)

// Future<T>::onFailed template.
// template <typename F, typename = typename std::result_of<F()>::type>
#define PROCESS_FUTURE_ONFAILED_F_LESSPREFER_TEMPLATE template <typename F, typename = decltype(std::declval<enable_if_t<!is_bind_expression<decay_t<F>>::value, F>>()())>  // NOLINT(whitespace/line_length)

// Future<T>::onAny template.
// template <typename F, typename = typename std::result_of<F()>::type>
#define PROCESS_FUTURE_ONANY_F_LESSPREFER_TEMPLATE template <typename F, typename = decltype(std::declval<enable_if_t<!is_bind_expression<decay_t<F>>::value, F>>()())>  // NOLINT(whitespace/line_length)

// Future<T>::then templates.
// template <typename F, typename X = typename internal::unwrap<typename std::result_of<F(const T&)>::type>::type> // NOLINT(whitespace/line_length)
#define PROCESS_FUTURE_THEN_DEFERRED_PREFER_TEMPLATE template <typename F, typename X = typename internal::unwrap<decltype(std::invoke(std::declval<F>(), std::declval<const T&>()))>::type> // NOLINT(whitespace/line_length)

// template <typename F, typename X = typename internal::unwrap<typename std::result_of<F()>::type>::type> // NOLINT(whitespace/line_length)
#define PROCESS_FUTURE_THEN_DEFERRED_LESSPREFER_TEMPLATE template <typename F, typename X = typename internal::unwrap<decltype(std::declval<enable_if_t<!is_bind_expression<decay_t<F>>::value, F>>()())>::type> // NOLINT(whitespace/line_length)

// template <typename F, typename X = typename internal::unwrap<typename std::result_of<F()>::type>::type> // NOLINT(whitespace/line_length)
#define PROCESS_FUTURE_THEN_F_PREFER_TEMPLATE template <typename F, typename X = typename internal::unwrap<decltype(std::invoke( std::declval<F>(), std::declval<const T&>()))>::type> // NOLINT(whitespace/line_length)

// template <typename F, typename X = typename internal::unwrap<typename std::result_of<F()>::type>::type> // NOLINT(whitespace/line_length)
#define PROCESS_FUTURE_THEN_F_LESSPREFER_TEMPLATE template <typename F, typename X = typename internal::unwrap<decltype(std::declval<enable_if_t<!is_bind_expression<decay_t<F>>::value, F>>()())>::type> // NOLINT(whitespace/line_length)

#endif // __PROCESS_WINDOWS_FUTURE_HPP__
