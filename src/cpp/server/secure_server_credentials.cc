/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <functional>
#include <map>
#include <memory>

#include <grpc++/impl/codegen/slice.h>
#include <grpc++/security/auth_metadata_processor.h>

#include "src/cpp/common/secure_auth_context.h"
#include "src/cpp/server/secure_server_credentials.h"

namespace grpc {

void AuthMetadataProcessorAyncWrapper::Destroy(void* wrapper) {
  auto* w = reinterpret_cast<AuthMetadataProcessorAyncWrapper*>(wrapper);
  delete w;
}

void AuthMetadataProcessorAyncWrapper::Process(
    void* wrapper, grpc_auth_context* context, const grpc_metadata* md,
    size_t num_md, grpc_process_auth_metadata_done_cb cb, void* user_data) {
  auto* w = reinterpret_cast<AuthMetadataProcessorAyncWrapper*>(wrapper);
  if (!w->processor_) {
    // Early exit.
    cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_OK, nullptr);
    return;
  }
  if (w->processor_->IsBlocking()) {
    w->thread_pool_->Add(
        std::bind(&AuthMetadataProcessorAyncWrapper::InvokeProcessor, w,
                  context, md, num_md, cb, user_data));
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
  SecureAuthContext context(ctx, false);
  AuthMetadataProcessor::OutputMetadata consumed_metadata;
  AuthMetadataProcessor::OutputMetadata response_metadata;

  Status status = processor_->Process(metadata, &context, &consumed_metadata,
                                      &response_metadata);

  std::vector<grpc_metadata> consumed_md;
  for (auto it = consumed_metadata.begin(); it != consumed_metadata.end();
       ++it) {
    grpc_metadata md_entry;
    md_entry.key = SliceReferencingString(it->first);
    md_entry.value = SliceReferencingString(it->second);
    md_entry.flags = 0;
    consumed_md.push_back(md_entry);
  }
  std::vector<grpc_metadata> response_md;
  for (auto it = response_metadata.begin(); it != response_metadata.end();
       ++it) {
    grpc_metadata md_entry;
    md_entry.key = SliceReferencingString(it->first);
    md_entry.value = SliceReferencingString(it->second);
    md_entry.flags = 0;
    response_md.push_back(md_entry);
  }
  auto consumed_md_data = consumed_md.empty() ? nullptr : &consumed_md[0];
  auto response_md_data = response_md.empty() ? nullptr : &response_md[0];
  cb(user_data, consumed_md_data, consumed_md.size(), response_md_data,
     response_md.size(), static_cast<grpc_status_code>(status.error_code()),
     status.error_message().c_str());
}

int SecureServerCredentials::AddPortToServer(const grpc::string& addr,
                                             grpc_server* server) {
  return grpc_server_add_secure_http2_port(server, addr.c_str(), creds_);
}

void SecureServerCredentials::SetAuthMetadataProcessor(
    const std::shared_ptr<AuthMetadataProcessor>& processor) {
  auto* wrapper = new AuthMetadataProcessorAyncWrapper(processor);
  grpc_server_credentials_set_auth_metadata_processor(
      creds_, {AuthMetadataProcessorAyncWrapper::Process,
               AuthMetadataProcessorAyncWrapper::Destroy, wrapper});
}

std::shared_ptr<ServerCredentials> SslServerCredentials(
    const SslServerCredentialsOptions& options) {
  std::vector<grpc_ssl_pem_key_cert_pair> pem_key_cert_pairs;
  for (auto key_cert_pair = options.pem_key_cert_pairs.begin();
       key_cert_pair != options.pem_key_cert_pairs.end(); key_cert_pair++) {
    grpc_ssl_pem_key_cert_pair p = {key_cert_pair->private_key.c_str(),
                                    key_cert_pair->cert_chain.c_str()};
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

}  // namespace grpc
