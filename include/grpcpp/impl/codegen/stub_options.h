/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPCPP_IMPL_CODEGEN_STUB_OPTIONS_H
#define GRPCPP_IMPL_CODEGEN_STUB_OPTIONS_H

// IWYU pragma: private, include <grpcpp/support/stub_options.h>

namespace grpc {

/// Useful interface for generated stubs
class StubOptions {
 public:
  StubOptions() = default;
  explicit StubOptions(const char* suffix_for_stats)
      : suffix_for_stats_(suffix_for_stats) {}

  void set_suffix_for_stats(const char* suffix_for_stats) {
    suffix_for_stats_ = suffix_for_stats;
  }
  const char* suffix_for_stats() const { return suffix_for_stats_; }

 private:
  const char* suffix_for_stats_ = nullptr;
};

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_STUB_OPTIONS_H
