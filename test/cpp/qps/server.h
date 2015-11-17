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

#ifndef TEST_QPS_SERVER_H
#define TEST_QPS_SERVER_H

#include <grpc/support/cpu.h>
#include <grpc++/security/server_credentials.h>

#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/cpp/qps/timer.h"
#include "test/proto/messages.grpc.pb.h"
#include "test/proto/benchmarks/control.grpc.pb.h"

namespace grpc {
namespace testing {

class Server {
 public:
  explicit Server(const ServerConfig& config) : timer_(new Timer) {
    if (config.port()) {
      port_ = config.port();
    } else {
      port_ = grpc_pick_unused_port_or_die();
    }
  }
  virtual ~Server() {}

  ServerStats Mark(bool reset) {
    Timer::Result timer_result;
    if (reset) {
      std::unique_ptr<Timer> timer(new Timer);
      timer.swap(timer_);
      timer_result = timer->Mark();
    } else {
      timer_result = timer_->Mark();
    }

    ServerStats stats;
    stats.set_time_elapsed(timer_result.wall);
    stats.set_time_system(timer_result.system);
    stats.set_time_user(timer_result.user);
    return stats;
  }

  static bool SetPayload(PayloadType type, int size, Payload* payload) {
    PayloadType response_type = type;
    // TODO(yangg): Support UNCOMPRESSABLE payload.
    if (type != PayloadType::COMPRESSABLE) {
      return false;
    }
    payload->set_type(response_type);
    std::unique_ptr<char[]> body(new char[size]());
    payload->set_body(body.get(), size);
    return true;
  }

  int port() const { return port_; }
  int cores() const { return gpr_cpu_num_cores(); }
  static std::shared_ptr<ServerCredentials> CreateServerCredentials(
      const ServerConfig& config) {
    if (config.has_security_params()) {
      SslServerCredentialsOptions::PemKeyCertPair pkcp = {test_server1_key,
                                                          test_server1_cert};
      SslServerCredentialsOptions ssl_opts;
      ssl_opts.pem_root_certs = "";
      ssl_opts.pem_key_cert_pairs.push_back(pkcp);
      return SslServerCredentials(ssl_opts);
    } else {
      return InsecureServerCredentials();
    }
  }

 private:
  int port_;
  std::unique_ptr<Timer> timer_;
};

std::unique_ptr<Server> CreateSynchronousServer(const ServerConfig& config);
std::unique_ptr<Server> CreateAsyncServer(const ServerConfig& config);

}  // namespace testing
}  // namespace grpc

#endif
