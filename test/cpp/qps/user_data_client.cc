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

#include "user_data_client.h"

namespace grpc {
namespace testing {

void UserDataClient::setConfigs(const ClientConfig& clientConfig, const ServerConfig& serverConfig) {
  clientConfig_ = clientConfig;
  serverConfig_ = serverConfig;
}

void UserDataClient::setQPS(double QPS) {
  QPS_ = QPS;
}

void UserDataClient::setQPSPerCore(double QPSPerCore) {
  QPSPerCore_ = QPSPerCore;
}

void UserDataClient::setLatencies(double percentileLatency50, double percentileLatency90,
    double percentileLatency95, double percentileLatency99, double percentileLatency99Point9) {
  percentileLatency50_ = percentileLatency50;
  percentileLatency90_ = percentileLatency90;
  percentileLatency95_ = percentileLatency95;
  percentileLatency99_ = percentileLatency99;
  percentileLatency99Point9_ = percentileLatency99Point9;
}

void UserDataClient::setTimes(double serverSystemTime, double serverUserTime, 
    double clientSystemTime, double clientUserTime) {
  serverSystemTime_ = serverSystemTime;
  serverUserTime_ = serverUserTime;
  clientSystemTime_ = clientSystemTime;
  clientUserTime_ = clientUserTime;
}

int UserDataClient::sendData(std::string access_token, std::string test_name) {

  SingleUserRecordRequest singleUserRecordRequest;
  singleUserRecordRequest.set_access_token(access_token);
  singleUserRecordRequest.set_test_name(test_name);
  *(singleUserRecordRequest.mutable_client_config()) = clientConfig_;
  *(singleUserRecordRequest.mutable_server_config()) = serverConfig_;
  
  Metrics* metrics = singleUserRecordRequest.mutable_metrics();

  if(QPS_ != DBL_MIN) metrics->set_qps(QPS_);
  if(QPSPerCore_ != DBL_MIN) metrics->set_qps_per_core(QPSPerCore_);
  if(percentileLatency50_ != DBL_MIN) metrics->set_perc_lat_50(percentileLatency50_);
  if(percentileLatency90_ != DBL_MIN) metrics->set_perc_lat_90(percentileLatency90_);
  if(percentileLatency95_ != DBL_MIN) metrics->set_perc_lat_95(percentileLatency95_);
  if(percentileLatency99_ != DBL_MIN) metrics->set_perc_lat_99(percentileLatency99_);
  if(percentileLatency99Point9_ != DBL_MIN) metrics->set_perc_lat_99_point_9(percentileLatency99Point9_);
  if(serverSystemTime_ != DBL_MIN) metrics->set_server_system_time(serverSystemTime_);
  if(serverUserTime_ != DBL_MIN) metrics->set_server_user_time(serverUserTime_);
  if(clientSystemTime_ != DBL_MIN) metrics->set_client_system_time(clientSystemTime_);
  if(clientUserTime_ != DBL_MIN) metrics->set_client_user_time(clientUserTime_);

  SingleUserRecordReply singleUserRecordReply;
  ClientContext context;

  Status status = stub_->RecordSingleClientData(&context, singleUserRecordRequest, &singleUserRecordReply);
  if (status.IsOk()) {
    return 1;
  } else {
    return -1;
  }
}

// Get current date/time, format is YYYY-MM-DD.HH:mm:ss
const std::string currentDateTime() {
  time_t     now = time(0);
  struct tm  tstruct;
  char       buf[80];
  tstruct = *localtime(&now);

  strftime(buf, sizeof(buf), "%Y/%m/%d, %X", &tstruct);
  return buf;
}

}
}