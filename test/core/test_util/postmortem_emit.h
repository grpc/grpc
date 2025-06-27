// Copyright 2025 gRPC authors.
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

#ifndef GRPC_TEST_CORE_TEST_UTIL_POSTMORTEM_EMIT_H
#define GRPC_TEST_CORE_TEST_UTIL_POSTMORTEM_EMIT_H

namespace grpc_core {

// Emit useful post mortem analysis from whatever in-process data we have.
void PostMortemEmit();

// Does all the work of PostMortemEmit, but doesn't emit anything.
// This is useful for verifying that PostMortemEmit *would* succeed...
// which means especially that channelz is working.
void SilentPostMortemEmit();

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_TEST_UTIL_POSTMORTEM_EMIT_H
