// Copyright 2022 gRPC authors.
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

#include "src/core/ext/filters/http/client_authority_filter.h"
#include "src/core/lib/security/transport/auth_filters.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/filters/filter_fuzzer.pb.h"

bool squeltch = true;

static const grpc_channel_filter* const kFilters[] = {
    &grpc_core::ClientAuthorityFilter::kFilter,
};

DEFINE_PROTO_FUZZER(const filter_fuzzer::Msg& msg) {}
