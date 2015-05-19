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

#ifndef TEST_QPS_REPORT_H
#define TEST_QPS_REPORT_H

#include <memory>
#include <set>
#include <vector>
#include <grpc++/config.h>

#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/qpstest.grpc.pb.h"

namespace grpc {
namespace testing {

/** General set of data required for report generation. */
struct ReportData {
  const ClientConfig& client_config;
  const ServerConfig& server_config;
  const ScenarioResult& scenario_result;
};

/** Specifies the type of performance report we are interested in.
 *
 *  \note The special type \c REPORT_ALL is equivalent to specifying all the
 *  other fields. */
enum ReportType {
  /** Equivalent to the combination of all other fields. */
  REPORT_ALL,
  /** Report only QPS information. */
  REPORT_QPS,
  /** Report only QPS per core information. */
  REPORT_QPS_PER_CORE,
  /** Report latency info for the 50, 90, 95, 99 and 99.9th percentiles. */
  REPORT_LATENCY,
  /** Report user and system time. */
  REPORT_TIMES
};

class Reporter;

/** Interface for all reporters. */
class Reporter {
 public:
  /** Construct a reporter with the given \a name. */
  Reporter(const string& name) : name_(name) {}

  /** Returns this reporter's name.
   *
   * Names are constants, set at construction time. */
  string name() const { return name_; }

  /** Template method responsible for the generation of the requested types. */
  void Report(const ReportData& data, const std::set<ReportType>& types) const;

 protected:
  /** Reports QPS for the given \a result. */
  virtual void ReportQPS(const ScenarioResult& result) const = 0;

  /** Reports QPS per core as (YYY/server core). */
  virtual void ReportQPSPerCore(const ScenarioResult& result,
                        const ServerConfig& config) const = 0;

  /** Reports latencies for the 50, 90, 95, 99 and 99.9 percentiles, in ms. */
  virtual void ReportLatency(const ScenarioResult& result) const = 0;

  /** Reports system and user time for client and server systems. */
  virtual void ReportTimes(const ScenarioResult& result) const = 0;

 private:
  const string name_;
};


// Reporters.

/** Reporter to gpr_log(GPR_INFO). */
class GprLogReporter : public Reporter {
 public:
  GprLogReporter(const string& name) : Reporter(name) {}

 private:
  void ReportQPS(const ScenarioResult& result) const GRPC_OVERRIDE;
  void ReportQPSPerCore(const ScenarioResult& result,
                        const ServerConfig& config) const GRPC_OVERRIDE;
  void ReportLatency(const ScenarioResult& result) const GRPC_OVERRIDE;
  void ReportTimes(const ScenarioResult& result) const GRPC_OVERRIDE;
};

}  // namespace testing
}  // namespace grpc

#endif
