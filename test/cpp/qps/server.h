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

#include "test/cpp/qps/timer.h"
#include "test/proto/qpstest.grpc.pb.h"

namespace grpc {
namespace testing {

class Server {
 public:
  Server() : timer_(new Timer) {}
  virtual ~Server() {}

  ServerStats Mark() {
    std::unique_ptr<Timer> timer(new Timer);
    timer.swap(timer_);

    auto timer_result = timer->Mark();

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

 private:
  std::unique_ptr<Timer> timer_;
};

std::unique_ptr<Server> CreateSynchronousServer(const ServerConfig& config,
                                                int port);
std::unique_ptr<Server> CreateAsyncServer(const ServerConfig& config, int port);

}  // namespace testing
}  // namespace grpc

#endif
