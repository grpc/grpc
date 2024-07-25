//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/ext/transport/chttp2/alpn/alpn.h"

#include "absl/log/check.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/util/useful.h"

// in order of preference
static const char* const supported_versions[] = {"h2"};

int grpc_chttp2_is_alpn_version_supported(const char* version, size_t size) {
  size_t i;
  for (i = 0; i < GPR_ARRAY_SIZE(supported_versions); i++) {
    if (size == strlen(supported_versions[i]) &&
        !strncmp(version, supported_versions[i], size)) {
      return 1;
    }
  }
  return 0;
}

size_t grpc_chttp2_num_alpn_versions(void) {
  return GPR_ARRAY_SIZE(supported_versions);
}

const char* grpc_chttp2_get_alpn_version_index(size_t i) {
  CHECK_LT(i, GPR_ARRAY_SIZE(supported_versions));
  return supported_versions[i];
}
