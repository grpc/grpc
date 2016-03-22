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

#include "test/cpp/qps/perf_db_client.h"

namespace grpc {
namespace testing {

// sets the client and server config information
void PerfDbClient::setConfigs(const ClientConfig& client_config,
                              const ServerConfig& server_config) {
  client_config_ = client_config;
  server_config_ = server_config;
}

// sets the QPS
void PerfDbClient::setQps(double qps) { qps_ = qps; }

// sets the QPS per core
void PerfDbClient::setQpsPerCore(double qps_per_core) {
  qps_per_core_ = qps_per_core;
}

// sets the 50th, 90th, 95th, 99th and 99.9th percentile latency
void PerfDbClient::setLatencies(double perc_lat_50, double perc_lat_90,
                                double perc_lat_95, double perc_lat_99,
                                double perc_lat_99_point_9) {
  perc_lat_50_ = perc_lat_50;
  perc_lat_90_ = perc_lat_90;
  perc_lat_95_ = perc_lat_95;
  perc_lat_99_ = perc_lat_99;
  perc_lat_99_point_9_ = perc_lat_99_point_9;
}

// sets the server and client, user and system times
void PerfDbClient::setTimes(double server_system_time, double server_user_time,
                            double client_system_time,
                            double client_user_time) {
  server_system_time_ = server_system_time;
  server_user_time_ = server_user_time;
  client_system_time_ = client_system_time;
  client_user_time_ = client_user_time;
}

// sends the data to the performance database server
bool PerfDbClient::sendData(std::string hashed_id, std::string test_name,
                            std::string sys_info, std::string tag) {
  // Data record request object
  SingleUserRecordRequest single_user_record_request;

  // setting access token, name of the test and the system information
  single_user_record_request.set_hashed_id(hashed_id);
  single_user_record_request.set_test_name(test_name);
  single_user_record_request.set_sys_info(sys_info);
  single_user_record_request.set_tag(tag);

  // setting configs
  *(single_user_record_request.mutable_client_config()) = client_config_;
  *(single_user_record_request.mutable_server_config()) = server_config_;

  Metrics* metrics = single_user_record_request.mutable_metrics();

  // setting metrcs in data record request
  if (qps_ != DBL_MIN) {
    metrics->set_qps(qps_);
  }
  if (qps_per_core_ != DBL_MIN) {
    metrics->set_qps_per_core(qps_per_core_);
  }
  if (perc_lat_50_ != DBL_MIN) {
    metrics->set_perc_lat_50(perc_lat_50_);
  }
  if (perc_lat_90_ != DBL_MIN) {
    metrics->set_perc_lat_90(perc_lat_90_);
  }
  if (perc_lat_95_ != DBL_MIN) {
    metrics->set_perc_lat_95(perc_lat_95_);
  }
  if (perc_lat_99_ != DBL_MIN) {
    metrics->set_perc_lat_99(perc_lat_99_);
  }
  if (perc_lat_99_point_9_ != DBL_MIN) {
    metrics->set_perc_lat_99_point_9(perc_lat_99_point_9_);
  }
  if (server_system_time_ != DBL_MIN) {
    metrics->set_server_system_time(server_system_time_);
  }
  if (server_user_time_ != DBL_MIN) {
    metrics->set_server_user_time(server_user_time_);
  }
  if (client_system_time_ != DBL_MIN) {
    metrics->set_client_system_time(client_system_time_);
  }
  if (client_user_time_ != DBL_MIN) {
    metrics->set_client_user_time(client_user_time_);
  }

  SingleUserRecordReply single_user_record_reply;
  ClientContext context;

  Status status = stub_->RecordSingleClientData(
      &context, single_user_record_request, &single_user_record_reply);
  if (status.ok()) {
    return true;  // data sent to database successfully
  } else {
    return false;  // error in data sending
  }
}
}  // testing
}  // grpc
