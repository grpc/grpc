// Copyright 2024 gRPC authors.
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

#include "src/core/client_channel/lb_metadata.h"

namespace grpc_core {

namespace {

class Encoder {
 public:
  void Encode(const Slice& key, const Slice& value) {
    out_.emplace_back(std::string(key.as_string_view()),
                      std::string(value.as_string_view()));
  }

  template <class Which>
  void Encode(Which, const typename Which::ValueType& value) {
    auto value_slice = Which::Encode(value);
    out_.emplace_back(std::string(Which::key()),
                      std::string(value_slice.as_string_view()));
  }

  void Encode(GrpcTimeoutMetadata,
              const typename GrpcTimeoutMetadata::ValueType&) {}
  void Encode(HttpPathMetadata, const Slice&) {}
  void Encode(HttpMethodMetadata,
              const typename HttpMethodMetadata::ValueType&) {}

  std::vector<std::pair<std::string, std::string>> Take() {
    return std::move(out_);
  }

 private:
  std::vector<std::pair<std::string, std::string>> out_;
};

}  // namespace

void LbMetadata::Add(absl::string_view key, absl::string_view value) {
  if (batch_ == nullptr) return;
  // Gross, egregious hack to support legacy grpclb behavior.
  // TODO(ctiller): Use a promise context for this once that plumbing is done.
  if (key == GrpcLbClientStatsMetadata::key()) {
    batch_->Set(
        GrpcLbClientStatsMetadata(),
        const_cast<GrpcLbClientStats*>(
            reinterpret_cast<const GrpcLbClientStats*>(value.data())));
    return;
  }
  batch_->Append(key, Slice::FromStaticString(value),
                 [key](absl::string_view error, const Slice& value) {
                   gpr_log(GPR_ERROR, "%s",
                           absl::StrCat(error, " key:", key,
                                        " value:", value.as_string_view())
                               .c_str());
                 });
}

std::vector<std::pair<std::string, std::string>>
LbMetadata::TestOnlyCopyToVector() {
  if (batch_ == nullptr) return {};
  Encoder encoder;
  batch_->Encode(&encoder);
  return encoder.Take();
}

absl::optional<absl::string_view> LbMetadata::Lookup(
    absl::string_view key, std::string* buffer) const {
  if (batch_ == nullptr) return absl::nullopt;
  return batch_->GetStringValue(key, buffer);
}

}  // namespace grpc_core
