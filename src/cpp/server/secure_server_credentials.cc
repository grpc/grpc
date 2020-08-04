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

#include <functional>
#include <map>
#include <memory>

#include <grpcpp/impl/codegen/slice.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/security/auth_metadata_processor.h>

#include "src/cpp/common/secure_auth_context.h"
#include "src/cpp/server/secure_server_credentials.h"

namespace grpc {

void AuthMetadataProcessorAyncWrapper::Destroy(void* wrapper) {
  auto* w = static_cast<AuthMetadataProcessorAyncWrapper*>(wrapper);
  delete w;
}

void AuthMetadataProcessorAyncWrapper::Process(
    void* wrapper, grpc_auth_context* context, const grpc_metadata* md,
    size_t num_md, grpc_process_auth_metadata_done_cb cb, void* user_data) {
  auto* w = static_cast<AuthMetadataProcessorAyncWrapper*>(wrapper);
  if (!w->processor_) {
    // Early exit.
    cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_OK, nullptr);
    return;
  }
  if (w->processor_->IsBlocking()) {
    w->thread_pool_->Add([w, context, md, num_md, cb, user_data] {
      w->AuthMetadataProcessorAyncWrapper::InvokeProcessor(context, md, num_md,
                                                           cb, user_data);
    });
  } else {
    // invoke directly.
    w->InvokeProcessor(context, md, num_md, cb, user_data);
  }
}

void AuthMetadataProcessorAyncWrapper::InvokeProcessor(
    grpc_auth_context* ctx, const grpc_metadata* md, size_t num_md,
    grpc_process_auth_metadata_done_cb cb, void* user_data) {
  AuthMetadataProcessor::InputMetadata metadata;
  for (size_t i = 0; i < num_md; i++) {
    metadata.insert(std::make_pair(StringRefFromSlice(&md[i].key),
                                   StringRefFromSlice(&md[i].value)));
  }
  SecureAuthContext context(ctx);
  AuthMetadataProcessor::OutputMetadata consumed_metadata;
  AuthMetadataProcessor::OutputMetadata response_metadata;

  Status status = processor_->Process(metadata, &context, &consumed_metadata,
                                      &response_metadata);

  std::vector<grpc_metadata> consumed_md;
  for (const auto& consumed : consumed_metadata) {
    grpc_metadata md_entry;
    md_entry.key = SliceReferencingString(consumed.first);
    md_entry.value = SliceReferencingString(consumed.second);
    md_entry.flags = 0;
    consumed_md.push_back(md_entry);
  }
  std::vector<grpc_metadata> response_md;
  for (const auto& response : response_metadata) {
    grpc_metadata md_entry;
    md_entry.key = SliceReferencingString(response.first);
    md_entry.value = SliceReferencingString(response.second);
    md_entry.flags = 0;
    response_md.push_back(md_entry);
  }
  auto consumed_md_data = consumed_md.empty() ? nullptr : &consumed_md[0];
  auto response_md_data = response_md.empty() ? nullptr : &response_md[0];
  cb(user_data, consumed_md_data, consumed_md.size(), response_md_data,
     response_md.size(), static_cast<grpc_status_code>(status.error_code()),
     status.error_message().c_str());
}

int SecureServerCredentials::AddPortToServer(const std::string& addr,
                                             grpc_server* server) {
  return grpc_server_add_secure_http2_port(server, addr.c_str(), creds_);
}

void SecureServerCredentials::SetAuthMetadataProcessor(
    const std::shared_ptr<grpc::AuthMetadataProcessor>& processor) {
  auto* wrapper = new grpc::AuthMetadataProcessorAyncWrapper(processor);
  grpc_server_credentials_set_auth_metadata_processor(
      creds_, {grpc::AuthMetadataProcessorAyncWrapper::Process,
               grpc::AuthMetadataProcessorAyncWrapper::Destroy, wrapper});
}

std::shared_ptr<ServerCredentials> SslServerCredentials(
    const grpc::SslServerCredentialsOptions& options) {
  std::vector<grpc_ssl_pem_key_cert_pair> pem_key_cert_pairs;
  for (const auto& key_cert_pair : options.pem_key_cert_pairs) {
    grpc_ssl_pem_key_cert_pair p = {key_cert_pair.private_key.c_str(),
                                    key_cert_pair.cert_chain.c_str()};
    pem_key_cert_pairs.push_back(p);
  }
  grpc_server_credentials* c_creds = grpc_ssl_server_credentials_create_ex(
      options.pem_root_certs.empty() ? nullptr : options.pem_root_certs.c_str(),
      pem_key_cert_pairs.empty() ? nullptr : &pem_key_cert_pairs[0],
      pem_key_cert_pairs.size(),
      options.force_client_auth
          ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
          : options.client_certificate_request,
      nullptr);
  return std::shared_ptr<ServerCredentials>(
      new SecureServerCredentials(c_creds));
}

namespace experimental {

std::shared_ptr<ServerCredentials> AltsServerCredentials(
    const AltsServerCredentialsOptions& /* options */) {
  grpc_alts_credentials_options* c_options =
      grpc_alts_credentials_server_options_create();
  grpc_server_credentials* c_creds =
      grpc_alts_server_credentials_create(c_options);
  grpc_alts_credentials_options_destroy(c_options);
  return std::shared_ptr<ServerCredentials>(
      new SecureServerCredentials(c_creds));
}

std::shared_ptr<ServerCredentials> LocalServerCredentials(
    grpc_local_connect_type type) {
  return std::shared_ptr<ServerCredentials>(
      new SecureServerCredentials(grpc_local_server_credentials_create(type)));
}

std::shared_ptr<ServerCredentials> TlsServerCredentials(
    const grpc::experimental::TlsCredentialsOptions& options) {
  grpc::GrpcLibraryCodegen init;
  return std::shared_ptr<ServerCredentials>(new SecureServerCredentials(
      grpc_tls_server_credentials_create(options.c_credentials_options())));
}

}  // namespace experimental
}  // namespace grpc
