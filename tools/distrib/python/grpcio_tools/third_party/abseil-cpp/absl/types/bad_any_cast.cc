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

#include "absl/types/bad_any_cast.h"

#ifndef ABSL_USES_STD_ANY

#include <cstdlib>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

bad_any_cast::~bad_any_cast() = default;

const char* bad_any_cast::what() const noexcept { return "Bad any cast"; }

namespace any_internal {

void ThrowBadAnyCast() {
#ifdef ABSL_HAVE_EXCEPTIONS
  throw bad_any_cast();
#else
  ABSL_RAW_LOG(FATAL, "Bad any cast");
  std::abort();
#endif
}

}  // namespace any_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_USES_STD_ANY
