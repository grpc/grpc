// Copyright 2023 gRPC authors.
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

#ifndef GRPC_TEST_CPP_END2END_XDS_XDS_END2END_TEST_LIB_H
#define GRPC_TEST_CPP_END2END_XDS_XDS_END2END_TEST_LIB_H

#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/ext/xds/xds_client.h"

namespace grpc_core {
namespace testing {

class XdsHttpFilterTest : public ::testing::Test {
 protected:
  XdsHttpFilterTest()
      : xds_client_(MakeXdsClient()),
        decode_context_{xds_client_.get(), xds_server_, nullptr,
                        upb_def_pool_.ptr(), upb_arena_.ptr()} {}

  static RefCountedPtr<XdsClient> MakeXdsClient() {
    grpc_error_handle error;
    auto bootstrap = GrpcXdsBootstrap::Create(
        "{\n"
        "  \"xds_servers\": [\n"
        "    {\n"
        "      \"server_uri\": \"xds.example.com\",\n"
        "      \"channel_creds\": [\n"
        "        {\"type\": \"google_default\"}\n"
        "      ]\n"
        "    }\n"
        "  ]\n"
        "}");
    if (!bootstrap.ok()) {
      Crash(absl::StrFormat("Error parsing bootstrap: %s",
                            bootstrap.status().ToString().c_str()));
    }
    return MakeRefCounted<XdsClient>(std::move(*bootstrap),
                                     /*transport_factory=*/nullptr,
                                     /*event_engine=*/nullptr, "foo agent",
                                     "foo version");
  }

  XdsExtension MakeXdsExtension(const grpc::protobuf::Message& message) {
    google::protobuf::Any any;
    any.PackFrom(message);
    type_url_storage_ =
        std::string(absl::StripPrefix(any.type_url(), "type.googleapis.com/"));
    serialized_storage_ = std::string(any.value());
    ValidationErrors::ScopedField field(
        &errors_, absl::StrCat("http_filter.value[", type_url_storage_, "]"));
    XdsExtension extension;
    extension.type = absl::string_view(type_url_storage_);
    extension.value = absl::string_view(serialized_storage_);
    extension.validation_fields.push_back(std::move(field));
    return extension;
  }

  const XdsHttpFilterImpl* GetFilter(absl::string_view type) {
    return registry_.GetFilterForType(
        absl::StripPrefix(type, "type.googleapis.com/"));
  }

  GrpcXdsBootstrap::GrpcXdsServer xds_server_;
  RefCountedPtr<XdsClient> xds_client_;
  upb::DefPool upb_def_pool_;
  upb::Arena upb_arena_;
  XdsResourceType::DecodeContext decode_context_;
  XdsHttpFilterRegistry registry_;
  ValidationErrors errors_;
  std::string type_url_storage_;
  std::string serialized_storage_;
};

}  // namespace testing
}  // namespace grpc_core

#endif  // GRPC_TEST_CPP_END2END_XDS_XDS_END2END_TEST_LIB_H
