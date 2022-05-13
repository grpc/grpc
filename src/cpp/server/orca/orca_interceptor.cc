//
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
//

#include "src/cpp/server/orca/orca_interceptor.h"

#include <grpcpp/ext/call_metric_recorder.h>
#include <grpcpp/ext/orca_load_reporter.h>
#include <grpcpp/server_builder.h>

#include "src/core/lib/transport/metadata_batch.h"

void grpc::experimental::OrcaServerInterceptor::Intercept(
    InterceptorBatchMethods* methods) {
  if (methods->QueryInterceptionHookPoint(
          InterceptionHookPoints::PRE_SEND_STATUS)) {
    auto trailers = methods->GetSendTrailingMetadata();
    if (trailers != nullptr) {
      auto context = info_->server_context();
      auto recorder = context->call_metric_recorder_.get();
      if (recorder) {
        auto serialized = recorder->CreateSerializedReport();
        std::string key =
            std::string(grpc_core::EndpointLoadMetricsBinMetadata::key());
        trailers->emplace(
            std::make_pair(std::move(key), std::move(serialized)));
      }
    }
  }
  methods->Proceed();
}

grpc::experimental::Interceptor*
grpc::experimental::OrcaServerInterceptorFactory::CreateServerInterceptor(
    ServerRpcInfo* info) {
  return new OrcaServerInterceptor(info);
}

void grpc::experimental::OrcaServerInterceptorFactory::Register(
    grpc::ServerBuilder* builder) {
  builder->internal_interceptor_creators_.push_back(
      absl::make_unique<OrcaServerInterceptorFactory>());
}

void grpc::experimental::RegisterCallMetricLoadReporter(
    grpc::ServerBuilder* builder) {
  grpc::experimental::OrcaServerInterceptorFactory::Register(builder);
}
