/*
 *
 * Copyright 2019 gRPC authors.
 *
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
 * limitations under the License.
 *
 */

#include <cstdarg>
#include <cstdio>
#include <vector>

#if defined(__GNUC__)

namespace std {

// CentOS 7 (GLIBC_2.17 / GLIBCXX_3.4.19) doesn't have a following symbol
// which was added to GLIBCXX_3.4.20. gRPC uses Debian 8 which has
// GLIBCXX_3.4.20 when building .net artifacts so artifacts can have symbols
// which are not available on CentOS 7.
// To support CentOS 7, missing symbols are provided as weak symbols.
void __attribute__((weak)) __throw_out_of_range_fmt(char const* fmt, ...) {
  va_list ap;
  char buf[1024];  // That should be big enough.

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  buf[sizeof(buf) - 1] = 0;
  va_end(ap);

  __throw_range_error(buf);
}

}  // namespace std

#endif  // defined(__GNUC__)
