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

#include <grpc++/support/config.h>

#include "test/cpp/qps/driver.h"
#include "test/cpp/qps/perf_db_client.h"

namespace grpc {
namespace testing {

/** Interface for all reporters. */
class Reporter {
 public:
  /** Construct a reporter with the given \a name. */
  Reporter(const string& name) : name_(name) {}

  virtual ~Reporter() {}

  /** Returns this reporter's name.
   *
   * Names are constants, set at construction time. */
  string name() const { return name_; }

  /** Reports QPS for the given \a result. */
  virtual void ReportQPS(const ScenarioResult& result) = 0;

  /** Reports QPS per core as (YYY/server core). */
  virtual void ReportQPSPerCore(const ScenarioResult& result) = 0;

  /** Reports latencies for the 50, 90, 95, 99 and 99.9 percentiles, in ms. */
  virtual void ReportLatency(const ScenarioResult& result) = 0;

  /** Reports system and user time for client and server systems. */
  virtual void ReportTimes(const ScenarioResult& result) = 0;

 private:
  const string name_;
};

/** A composite for all reporters to be considered. */
class CompositeReporter : public Reporter {
 public:
  CompositeReporter() : Reporter("CompositeReporter") {}

  /** Adds a \a reporter to the composite. */
  void add(std::unique_ptr<Reporter> reporter);

  void ReportQPS(const ScenarioResult& result) GRPC_OVERRIDE;
  void ReportQPSPerCore(const ScenarioResult& result) GRPC_OVERRIDE;
  void ReportLatency(const ScenarioResult& result) GRPC_OVERRIDE;
  void ReportTimes(const ScenarioResult& result) GRPC_OVERRIDE;

 private:
  std::vector<std::unique_ptr<Reporter> > reporters_;
};

/** Reporter to gpr_log(GPR_INFO). */
class GprLogReporter : public Reporter {
 public:
  GprLogReporter(const string& name) : Reporter(name) {}

 private:
  void ReportQPS(const ScenarioResult& result) GRPC_OVERRIDE;
  void ReportQPSPerCore(const ScenarioResult& result) GRPC_OVERRIDE;
  void ReportLatency(const ScenarioResult& result) GRPC_OVERRIDE;
  void ReportTimes(const ScenarioResult& result) GRPC_OVERRIDE;
};

/** Reporter for performance database tool */
class PerfDbReporter : public Reporter {
 public:
  PerfDbReporter(const string& name, const string& hashed_id,
                 const string& test_name, const string& sys_info,
                 const string& server_address, const string& tag)
      : Reporter(name),
        hashed_id_(hashed_id),
        test_name_(test_name),
        sys_info_(sys_info),
        tag_(tag) {
    perf_db_client_.init(
        grpc::CreateChannel(server_address, grpc::InsecureCredentials()));
  }
  ~PerfDbReporter() GRPC_OVERRIDE { SendData(); };

 private:
  PerfDbClient perf_db_client_;
  std::string hashed_id_;
  std::string test_name_;
  std::string sys_info_;
  std::string tag_;
  void ReportQPS(const ScenarioResult& result) GRPC_OVERRIDE;
  void ReportQPSPerCore(const ScenarioResult& result) GRPC_OVERRIDE;
  void ReportLatency(const ScenarioResult& result) GRPC_OVERRIDE;
  void ReportTimes(const ScenarioResult& result) GRPC_OVERRIDE;
  void SendData();
};

}  // namespace testing
}  // namespace grpc

#endif
