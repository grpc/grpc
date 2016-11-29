/*
 *
 * Copyright 2015-2016, Google Inc.
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

#ifndef GRPC_TEST_CPP_UTIL_CREATE_TEST_CHANNEL_H
#define GRPC_TEST_CPP_UTIL_CREATE_TEST_CHANNEL_H

#include <memory>

#include <grpc++/security/credentials.h>

namespace grpc {
class Channel;

std::shared_ptr<Channel> CreateTestChannel(const grpc::string& server,
                                           bool enable_ssl);

std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& override_hostname,
    bool enable_ssl, bool use_prod_roots);

std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& override_hostname,
    bool enable_ssl, bool use_prod_roots,
    const std::shared_ptr<CallCredentials>& creds);

std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& override_hostname,
    bool enable_ssl, bool use_prod_roots,
    const std::shared_ptr<CallCredentials>& creds,
    const ChannelArguments& args);

std::shared_ptr<Channel> CreateTestChannel(
    const grpc::string& server, const grpc::string& credential_type,
    const std::shared_ptr<CallCredentials>& creds);

}  // namespace grpc

#endif  // GRPC_TEST_CPP_UTIL_CREATE_TEST_CHANNEL_H
