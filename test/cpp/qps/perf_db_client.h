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

#include <cfloat>
#include <iostream>
#include <memory>
#include <string>

#include <grpc++/channel.h>
#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>
#include <grpc++/security/credentials.h>
#include <grpc++/support/channel_arguments.h>
#include <grpc/grpc.h>
#include "src/proto/grpc/testing/perf_db.grpc.pb.h"

namespace grpc {
namespace testing {

// Manages data sending to performance database server
class PerfDbClient {
 public:
  PerfDbClient() {
    qps_ = DBL_MIN;
    qps_per_core_ = DBL_MIN;
    perc_lat_50_ = DBL_MIN;
    perc_lat_90_ = DBL_MIN;
    perc_lat_95_ = DBL_MIN;
    perc_lat_99_ = DBL_MIN;
    perc_lat_99_point_9_ = DBL_MIN;
    server_system_time_ = DBL_MIN;
    server_user_time_ = DBL_MIN;
    client_system_time_ = DBL_MIN;
    client_user_time_ = DBL_MIN;
  }

  void init(std::shared_ptr<Channel> channel) {
    stub_ = PerfDbTransfer::NewStub(channel);
  }

  ~PerfDbClient() {}

  // sets the client and server config information
  void setConfigs(const ClientConfig& client_config,
                  const ServerConfig& server_config);

  // sets the qps
  void setQps(double qps);

  // sets the qps per core
  void setQpsPerCore(double qps_per_core);

  // sets the 50th, 90th, 95th, 99th and 99.9th percentile latency
  void setLatencies(double perc_lat_50, double perc_lat_90, double perc_lat_95,
                    double perc_lat_99, double perc_lat_99_point_9);

  // sets the server and client, user and system times
  void setTimes(double server_system_time, double server_user_time,
                double client_system_time, double client_user_time);

  // sends the data to the performance database server
  bool sendData(std::string hashed_id, std::string test_name,
                std::string sys_info, std::string tag);

 private:
  std::unique_ptr<PerfDbTransfer::Stub> stub_;
  ClientConfig client_config_;
  ServerConfig server_config_;
  double qps_;
  double qps_per_core_;
  double perc_lat_50_;
  double perc_lat_90_;
  double perc_lat_95_;
  double perc_lat_99_;
  double perc_lat_99_point_9_;
  double server_system_time_;
  double server_user_time_;
  double client_system_time_;
  double client_user_time_;
};

}  // namespace testing
}  // namespace grpc
