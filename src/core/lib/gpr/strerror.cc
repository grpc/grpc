/*
 *
 * Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gpr/strerror.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

// The implementation below is mostly copied from abseil because it is internal
// there.
namespace grpc_core {
namespace {

// `ErrnoSaver` captures the value of `errno` upon construction and restores it
// upon deletion.  It is used in low-level code and must be super fast.  Do not
// add instrumentation, even in debug modes.
class ErrnoSaver {
 public:
  ErrnoSaver() : saved_errno_(errno) {}
  ~ErrnoSaver() { errno = saved_errno_; }
  int operator()() const { return saved_errno_; }

 private:
  const int saved_errno_;
};

const char* StrErrorAdaptor(int errnum, char* buf, size_t buflen) {
#if defined(GPR_WINDOWS)
  int rc = strerror_s(buf, buflen, errnum);
  buf[buflen - 1] = '\0';  // guarantee NUL termination
  if (rc == 0 && strncmp(buf, "Unknown error", buflen) == 0) *buf = '\0';
  return buf;
#else
  // The type of `ret` is platform-specific; both of these branches must compile
  // either way but only one will execute on any given platform:
  auto ret = strerror_r(errnum, buf, buflen);
  if (std::is_same<decltype(ret), int>::value) {
    // XSI `strerror_r`; `ret` is `int`:
    if (ret) *buf = '\0';
    return buf;
  } else {
    // GNU `strerror_r`; `ret` is `char *`:
    return reinterpret_cast<const char*>(ret);
  }
#endif
}

std::string StrErrorInternal(int errnum) {
  char buf[100];
  const char* str = StrErrorAdaptor(errnum, buf, sizeof buf);
  if (*str == '\0') {
    snprintf(buf, sizeof buf, "Unknown error %d", errnum);
    str = buf;
  }
  return str;
}

// kSysNerr is the number of errors from a recent glibc. `StrError()` falls back
// to `StrErrorAdaptor()` if the value is larger than this.
constexpr int kSysNerr = 135;

std::array<std::string, kSysNerr>* NewStrErrorTable() {
  auto* table = new std::array<std::string, kSysNerr>;
  for (int i = 0; i < static_cast<int>(table->size()); ++i) {
    (*table)[i] = StrErrorInternal(i);
  }
  return table;
}

}  // namespace

std::string StrError(int errnum) {
  ErrnoSaver errno_saver;
  static const auto* table = NewStrErrorTable();
  if (errnum >= 0 && errnum < static_cast<int>(table->size())) {
    return (*table)[errnum];
  }
  return StrErrorInternal(errnum);
}

}  // namespace grpc_core
