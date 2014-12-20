/*
 *
 * Copyright 2014, Google Inc.
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

#include "test/cpp/util/create_test_channel.h"

#include "test/core/end2end/data/ssl_test_data.h"
#include <grpc++/channel_arguments.h>
#include <grpc++/create_channel.h>
#include <grpc++/credentials.h>

namespace grpc {

// When ssl is enabled, if server is empty, override_hostname is used to
// create channel. Otherwise, connect to server and override hostname if
// override_hostname is provided.
// When ssl is not enabled, override_hostname is ignored.
// Set use_prod_root to true to use the SSL root for production GFE. Otherwise,
// root for test SSL cert will be used.
// Use examples:
//   CreateTestChannel("1.1.1.1:12345", "override.hostname.com", true, false);
//   CreateTestChannel("test.google.com:443", "", true, true);
//   CreateTestChannel("", "test.google.com:443", true, true);  // same as above
std::shared_ptr<ChannelInterface> CreateTestChannel(
    const grpc::string& server, const grpc::string& override_hostname,
    bool enable_ssl, bool use_prod_roots) {
  ChannelArguments channel_args;
  if (enable_ssl) {
    const char* roots_certs =
        use_prod_roots ? reinterpret_cast<const char*>(prod_roots_certs)
                       : reinterpret_cast<const char*>(test_root_cert);
    unsigned int roots_certs_size =
        use_prod_roots ? prod_roots_certs_size : test_root_cert_size;
    SslCredentialsOptions ssl_opts = {{roots_certs, roots_certs_size}, "", ""};

    std::unique_ptr<Credentials> creds =
        CredentialsFactory::SslCredentials(ssl_opts);

    if (!server.empty() && !override_hostname.empty()) {
      channel_args.SetSslTargetNameOverride(override_hostname);
    }
    const grpc::string& connect_to =
        server.empty() ? override_hostname : server;
    return CreateChannel(connect_to, creds, channel_args);
  } else {
    return CreateChannel(server, channel_args);
  }
}

// Shortcut for end2end and interop tests.
std::shared_ptr<ChannelInterface> CreateTestChannel(const grpc::string& server,
                                                    bool enable_ssl) {
  return CreateTestChannel(server, "foo.test.google.com", enable_ssl, false);
}

}  // namespace grpc
