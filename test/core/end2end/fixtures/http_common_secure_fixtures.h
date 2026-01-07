//
//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_TEST_CORE_END2END_FIXTURES_HTTP_COMMON_SECURE_FIXTURES_H
#define GRPC_TEST_CORE_END2END_FIXTURES_HTTP_COMMON_SECURE_FIXTURES_H

#include <grpc/credentials.h>
#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/status.h>

#include <cstddef>
#include <string>

#include "src/core/credentials/transport/fake/fake_credentials.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/util/grpc_check.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/proxy.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/test_util/tls_utils.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"

#endif  // GRPC_TEST_CORE_END2END_FIXTURES_HTTP_COMMON_SECURE_FIXTURES_H
