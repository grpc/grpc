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

#ifndef TEST_QPS_SERVER_H
#define TEST_QPS_SERVER_H

#include <grpc++/resource_quota.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server_builder.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <vector>

#include "src/core/lib/surface/completion_queue.h"
#include "src/proto/grpc/testing/control.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "test/core/end2end/data/ssl_test_data.h"
#include "test/core/util/port.h"
#include "test/cpp/qps/usage_timer.h"

namespace grpc {
namespace testing {

class Server {
 public:
  explicit Server(const ServerConfig& config)
      : timer_(new UsageTimer), last_reset_poll_count_(0) {
    cores_ = gpr_cpu_num_cores();
    if (config.port()) {
      port_ = config.port();

    } else {
      port_ = grpc_pick_unused_port_or_die();
    }
  }
  virtual ~Server() {}

  ServerStats Mark(bool reset) {
    UsageTimer::Result timer_result;
    int cur_poll_count = GetPollCount();
    int poll_count = cur_poll_count - last_reset_poll_count_;
    if (reset) {
      std::unique_ptr<UsageTimer> timer(new UsageTimer);
      timer.swap(timer_);
      timer_result = timer->Mark();
      last_reset_poll_count_ = cur_poll_count;
    } else {
      timer_result = timer_->Mark();
    }

    ServerStats stats;
    stats.set_time_elapsed(timer_result.wall);
    stats.set_time_system(timer_result.system);
    stats.set_time_user(timer_result.user);
    stats.set_total_cpu_time(timer_result.total_cpu_time);
    stats.set_idle_cpu_time(timer_result.idle_cpu_time);
    stats.set_cq_poll_count(poll_count);
    return stats;
  }

  static bool SetPayload(PayloadType type, int size, Payload* payload) {
    // TODO(yangg): Support UNCOMPRESSABLE payload.
    if (type != PayloadType::COMPRESSABLE) {
      return false;
    }
    payload->set_type(type);
    std::unique_ptr<char[]> body(new char[size]());
    payload->set_body(body.get(), size);
    return true;
  }

  int port() const { return port_; }
  int cores() const { return cores_; }
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

  static void ApplyServerConfig(const ServerConfig& config,
                                ServerBuilder* builder) {
    if (config.resource_quota_size() > 0) {
      builder->SetResourceQuota(
          ResourceQuota("QpsServerTest").Resize(config.resource_quota_size()));
    }

    for (auto arg : config.channel_args()) {
      switch (arg.value_case()) {
        case ChannelArg::kStrValue:
          builder->AddChannelArgument(arg.name(), arg.str_value());
          break;
        case ChannelArg::kIntValue:
          builder->AddChannelArgument(arg.name(), arg.int_value());
          break;
        default:
          gpr_log(GPR_ERROR, "Channel arg '%s' ignored due to unknown type",
                  arg.name().c_str());
      }
    }
  }

  virtual int GetPollCount() {
    // For sync server.
    return 0;
  }

 private:
  int port_;
  int cores_;
  std::unique_ptr<UsageTimer> timer_;
  int last_reset_poll_count_;
};

std::unique_ptr<Server> CreateSynchronousServer(const ServerConfig& config);
std::unique_ptr<Server> CreateAsyncServer(const ServerConfig& config);
std::unique_ptr<Server> CreateAsyncGenericServer(const ServerConfig& config);

}  // namespace testing
}  // namespace grpc

#endif
