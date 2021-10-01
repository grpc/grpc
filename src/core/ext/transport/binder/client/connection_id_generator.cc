// Copyright 2021 gRPC authors.
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

#include "src/core/ext/transport/binder/client/connection_id_generator.h"


namespace grpc_binder {

std::string ConnectionIdGenerator::Generate(absl::string_view package_name,
                                            absl::string_view class_name) {
  std::string s;
  for (auto c : package_name) {
    s += (isalnum(c) ? c : '0');
  }
  for (auto c : class_name) {
    s += (isalnum(c) ? c : '0');
  }
  if (s.length() > kPrefixLengthLimit) {
    s = s.substr(s.length() - kPrefixLengthLimit, kPrefixLengthLimit);
  }
  std::string ret;
  {
    grpc_core::MutexLock l(&m_);
    ret = s + std::to_string(++prefix_count_[s]);
  }
  return ret;
}

ConnectionIdGenerator* GetConnectionIdGenerator() {
  static ConnectionIdGenerator* cig = new ConnectionIdGenerator();
  return cig;
}

}  // namespace grpc_binder
