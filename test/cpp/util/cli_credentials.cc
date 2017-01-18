/*
 *
 * Copyright 2016, Google Inc.
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

#include "test/cpp/util/cli_credentials.h"

#include <fstream>
#include <sstream>
#include <gflags/gflags.h>

DEFINE_bool(enable_ssl, false, "Whether to use ssl/tls.");
DEFINE_bool(use_auth, false, "Whether to create default google credentials.");
DEFINE_string(ssl_server_roots_file, "", "Path to custom server roots file. If this option is not "
    "specified, the default server roots will be used. --enable_ssl must be set.");
DEFINE_string(ssl_client_cert_chain_file, "", "Path to client certificate chain file. "
    "--enable_ssl must be set.");
DEFINE_string(ssl_client_key_file, "", "Path to client key file. --enable_ssl must be set.");

namespace grpc {
namespace testing {

std::shared_ptr<grpc::ChannelCredentials> CliCredentials::GetCredentials()
    const {
  if (!FLAGS_enable_ssl) {
    return grpc::InsecureChannelCredentials();
  } else {
    if (FLAGS_use_auth) {
      return grpc::GoogleDefaultCredentials();
    } else {
      auto options = grpc::SslCredentialsOptions();
      if (!FLAGS_ssl_server_roots_file.empty()) {
        std::stringstream ca_buffer;
        ca_buffer << std::ifstream(FLAGS_ssl_server_roots_file).rdbuf();
        if (ca_buffer.str().empty()) {
          exit(2);
        }
        options.pem_root_certs = ca_buffer.str();
      }
      if ((FLAGS_ssl_client_cert_chain_file.empty() && !FLAGS_ssl_client_key_file.empty())
          || (!FLAGS_ssl_client_cert_chain_file.empty() && FLAGS_ssl_client_key_file.empty()) {
        std::cerr << "Both --ssl_client_cert_chain_file and --ssl_client_key_file must be specified." << std::endl;
        exit(2);
      }
      if (!FLAGS_ssl_client_cert_chain_file.empty() && !FLAGS_ssl_client_key_file.empty()) {
        std::stringstream cert_buffer;
        cert_buffer << std::ifstream(FLAGS_ssl_client_cert_chain_file).rdbuf();

        std::stringstream key_buffer;
        key_buffer << std::ifstream(FLAGS_ssl_client_key_file).rdbuf();

        if (cert_buffer.str().empty()) {
          std::cerr << "Failed to parse client certificate chain file." << std::endl;
          exit(2);
        }
        if (key_buffer.str().empty()) {
          std::cerr << "Failed to parse client key file." << std::endl;
          exit(2);
        }

        options.pem_cert_chain = cert_buffer.str();
        options.pem_private_key = key_buffer.str();
      }
      return grpc::SslCredentials(options);
    }
  }
}

const grpc::string CliCredentials::GetCredentialUsage() const {
  return "    --enable_ssl             ; Set whether to use tls\n"
         "    --use_auth               ; Set whether to create default google"
         " credentials\n";
}
}  // namespace testing
}  // namespace grpc
