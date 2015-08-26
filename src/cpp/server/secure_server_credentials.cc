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


#include "src/cpp/common/secure_auth_context.h"
#include "src/cpp/server/secure_server_credentials.h"

#include <grpc++/auth_metadata_processor.h>

namespace grpc {

void AuthMetadataProcessorAyncWrapper::Process(
    void* wrapper, grpc_auth_context* context, const grpc_metadata* md,
    size_t num_md, grpc_process_auth_metadata_done_cb cb, void* user_data) {
  auto* w = reinterpret_cast<AuthMetadataProcessorAyncWrapper*>(wrapper);
  if (w->processor_ == nullptr) {
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
    grpc_auth_context* ctx,
    const grpc_metadata* md, size_t num_md,
    grpc_process_auth_metadata_done_cb cb, void* user_data) {
  Metadata metadata;
  for (size_t i = 0; i < num_md; i++) {
    metadata.insert(std::make_pair(
        md[i].key, grpc::string(md[i].value, md[i].value_length)));
  }
  SecureAuthContext context(ctx);
  Metadata consumed_metadata;
  bool ok = processor_->Process(metadata, &context, &consumed_metadata);
  if (ok) {
    std::vector<grpc_metadata> consumed_md(consumed_metadata.size());
    for (const auto& entry : consumed_metadata) {
      consumed_md.push_back({entry.first.c_str(),
                             entry.second.data(),
                             entry.second.size(),
                             0,
                             {{nullptr, nullptr, nullptr, nullptr}}});
    }
    cb(user_data, &consumed_md[0], consumed_md.size(), nullptr, 0,
       GRPC_STATUS_OK, nullptr);
  } else {
    cb(user_data, nullptr, 0, nullptr, 0, GRPC_STATUS_UNAUTHENTICATED, nullptr);
  }
}

int SecureServerCredentials::AddPortToServer(const grpc::string& addr,
                                             grpc_server* server) {
  return grpc_server_add_secure_http2_port(server, addr.c_str(), creds_);
}

void SecureServerCredentials::SetAuthMetadataProcessor(
    const std::shared_ptr<AuthMetadataProcessor>& processor) {
  processor_.reset(new AuthMetadataProcessorAyncWrapper(processor));
  grpc_server_credentials_set_auth_metadata_processor(
      creds_, {AuthMetadataProcessorAyncWrapper::Process, processor_.get()});
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
  grpc_server_credentials* c_creds = grpc_ssl_server_credentials_create(
      options.pem_root_certs.empty() ? nullptr : options.pem_root_certs.c_str(),
      &pem_key_cert_pairs[0], pem_key_cert_pairs.size(),
      options.force_client_auth);
  return std::shared_ptr<ServerCredentials>(
      new SecureServerCredentials(c_creds));
}

}  // namespace grpc
