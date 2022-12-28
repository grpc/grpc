//
//
// Copyright 2016 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/support/port_platform.h>

#include <string.h>

// Provide a wrapped memcpy for targets that need to be backwards
// compatible with older libc's.
//
// Enable by setting LDFLAGS=-Wl,-wrap,memcpy when linking.
//

extern "C" {
#ifdef __linux__
#if defined(__x86_64__) && !defined(GPR_MUSL_LIBC_COMPAT) && \
    !defined(__ANDROID__)
__asm__(".symver memcpy,memcpy@GLIBC_2.2.5");
void* __wrap_memcpy(void* destination, const void* source, size_t num) {
  return memcpy(destination, source, num);
}
#else  // !__x86_64__
void* __wrap_memcpy(void* destination, const void* source, size_t num) {
  return memmove(destination, source, num);
}
#endif
#endif
}
