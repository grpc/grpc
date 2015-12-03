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

#ifndef TEST_QPS_DRIVER_H
#define TEST_QPS_DRIVER_H

#include <memory>

#include "test/cpp/qps/histogram.h"
#include "test/proto/benchmarks/control.grpc.pb.h"

namespace grpc {
namespace testing {
class ResourceUsage {
 public:
  ResourceUsage(double w, double u, double s, int c)
      : wall_time_(w), user_time_(u), system_time_(s), cores_(c) {}
  double wall_time() const { return wall_time_; }
  double user_time() const { return user_time_; }
  double system_time() const { return system_time_; }
  int cores() const { return cores_; }

 private:
  double wall_time_;
  double user_time_;
  double system_time_;
  int cores_;
};

struct ScenarioResult {
  Histogram latencies;
  std::vector<ResourceUsage> client_resources;
  std::vector<ResourceUsage> server_resources;
  ClientConfig client_config;
  ServerConfig server_config;
};

std::unique_ptr<ScenarioResult> RunScenario(
    const grpc::testing::ClientConfig& client_config, size_t num_clients,
    const grpc::testing::ServerConfig& server_config, size_t num_servers,
    int warmup_seconds, int benchmark_seconds, int spawn_local_worker_count);

}  // namespace testing
}  // namespace grpc

#endif
