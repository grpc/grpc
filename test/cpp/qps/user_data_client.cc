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

void UserDataClient::setAccessToken(std::string access_token) {
  access_token_ = access_token;
}

void UserDataClient::setQPS(double QPS) {
  QPS_ = QPS;
  qpsSet = true;
}

void UserDataClient::setQPSPerCore(double qpsPerCore) {
  //TBD
}

void UserDataClient::setLatencies(double percentileLatency50, double percentileLatency90,
    double percentileLatency95, double percentileLatency99, double percentileLatency99Point9) {
  percentileLatency50_ = percentileLatency50;
  percentileLatency90_ = percentileLatency90;
  percentileLatency95_ = percentileLatency95;
  percentileLatency99_ = percentileLatency99;
  percentileLatency99Point9_ = percentileLatency99Point9;

  latenciesSet = true;
}

void UserDataClient::setTimes(double serverSystemTime, double serverUserTime, 
    double clientSystemTime, double clientUserTime) {
  //TBD
}

int UserDataClient::sendDataIfReady() {
  if(!(qpsSet && latenciesSet))
    return 0;

  SingleUserRecordRequest singleUserRecordRequest;
  singleUserRecordRequest.set_access_token(access_token_);
  
  Metrics* metrics = singleUserRecordRequest.mutable_metrics();
  metrics->set_qps(QPS_);
  metrics->set_perc_lat_50(percentileLatency50_);
  metrics->set_perc_lat_90(percentileLatency90_);
  metrics->set_perc_lat_95(percentileLatency95_);
  metrics->set_perc_lat_99(percentileLatency99_);
  metrics->set_perc_lat_99_point_9(percentileLatency99Point9_);

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