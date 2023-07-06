//
// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

// These routines provide mem versions of standard C string routines,
// such as strpbrk.  They function exactly the same as the str versions,
// so if you wonder what they are, replace the word "mem" by
// "str" and check out the man page.  I could return void*, as the
// strutil.h mem*() routines tend to do, but I return char* instead
// since this is by far the most common way these functions are called.
//
// The difference between the mem and str versions is the mem version
// takes a pointer and a length, rather than a '\0'-terminated string.
// The memcase* routines defined here assume the locale is "C"
// (they use absl::ascii_tolower instead of tolower).
//
// These routines are based on the BSD library.
//
// Here's a list of routines from string.h, and their mem analogues.
// Functions in lowercase are defined in string.h; those in UPPERCASE
// are defined here:
//
// strlen                  --
// strcat strncat          MEMCAT
// strcpy strncpy          memcpy
// --                      memccpy   (very cool function, btw)
// --                      memmove
// --                      memset
// strcmp strncmp          memcmp
// strcasecmp strncasecmp  MEMCASECMP
// strchr                  memchr
// strcoll                 --
// strxfrm                 --
// strdup strndup          MEMDUP
// strrchr                 MEMRCHR
// strspn                  MEMSPN
// strcspn                 MEMCSPN
// strpbrk                 MEMPBRK
// strstr                  MEMSTR MEMMEM
// (g)strcasestr           MEMCASESTR MEMCASEMEM
// strtok                  --
// strprefix               MEMPREFIX      (strprefix is from strutil.h)
// strcaseprefix           MEMCASEPREFIX  (strcaseprefix is from strutil.h)
// strsuffix               MEMSUFFIX      (strsuffix is from strutil.h)
// strcasesuffix           MEMCASESUFFIX  (strcasesuffix is from strutil.h)
// --                      MEMIS
// --                      MEMCASEIS
// strcount                MEMCOUNT       (strcount is from strutil.h)

#ifndef ABSL_STRINGS_INTERNAL_MEMUTIL_H_
#define ABSL_STRINGS_INTERNAL_MEMUTIL_H_

#include <cstddef>
#include <cstring>

#include "absl/base/port.h"  // disable some warnings on Windows
#include "absl/strings/ascii.h"  // for absl::ascii_tolower

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

inline char* memcat(char* dest, size_t destlen, const char* src,
                    size_t srclen) {
  return reinterpret_cast<char*>(memcpy(dest + destlen, src, srclen));
}

int memcasecmp(const char* s1, const char* s2, size_t len);
char* memdup(const char* s, size_t slen);
char* memrchr(const char* s, int c, size_t slen);
size_t memspn(const char* s, size_t slen, const char* accept);
size_t memcspn(const char* s, size_t slen, const char* reject);
char* mempbrk(const char* s, size_t slen, const char* accept);

// This is for internal use only.  Don't call this directly
template <bool case_sensitive>
const char* int_memmatch(const char* haystack, size_t haylen,
                         const char* needle, size_t neelen) {
  if (0 == neelen) {
    return haystack;  // even if haylen is 0
  }
  const char* hayend = haystack + haylen;
  const char* needlestart = needle;
  const char* needleend = needlestart + neelen;

  for (; haystack < hayend; ++haystack) {
    char hay = case_sensitive
                   ? *haystack
                   : absl::ascii_tolower(static_cast<unsigned char>(*haystack));
    char nee = case_sensitive
                   ? *needle
                   : absl::ascii_tolower(static_cast<unsigned char>(*needle));
    if (hay == nee) {
      if (++needle == needleend) {
        return haystack + 1 - neelen;
      }
    } else if (needle != needlestart) {
      // must back up haystack in case a prefix matched (find "aab" in "aaab")
      haystack -= needle - needlestart;  // for loop will advance one more
      needle = needlestart;
    }
  }
  return nullptr;
}

// These are the guys you can call directly
inline const char* memstr(const char* phaystack, size_t haylen,
                          const char* pneedle) {
  return int_memmatch<true>(phaystack, haylen, pneedle, strlen(pneedle));
}

inline const char* memcasestr(const char* phaystack, size_t haylen,
                              const char* pneedle) {
  return int_memmatch<false>(phaystack, haylen, pneedle, strlen(pneedle));
}

inline const char* memmem(const char* phaystack, size_t haylen,
                          const char* pneedle, size_t needlelen) {
  return int_memmatch<true>(phaystack, haylen, pneedle, needlelen);
}

inline const char* memcasemem(const char* phaystack, size_t haylen,
                              const char* pneedle, size_t needlelen) {
  return int_memmatch<false>(phaystack, haylen, pneedle, needlelen);
}

// This is significantly faster for case-sensitive matches with very
// few possible matches.  See unit test for benchmarks.
const char* memmatch(const char* phaystack, size_t haylen, const char* pneedle,
                     size_t neelen);

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_MEMUTIL_H_
