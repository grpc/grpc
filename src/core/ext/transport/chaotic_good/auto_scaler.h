// Copyright 2024 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CHAOTIC_GOOD_AUTOSCALER_H
#define CHAOTIC_GOOD_AUTOSCALER_H

#include "absl/container/flat_hash_map.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/tdigest.h"
#include "src/core/util/time.h"

namespace grpc_core {
namespace chaotic_good {

namespace autoscaler_detail {

struct Metrics {
  static double compression() { return 100.0; }
  Metrics() : client_latency(compression()), server_latency(compression()) {}
  Metrics(TDigest client_latency, TDigest server_latency)
      : client_latency(std::move(client_latency)),
        server_latency(std::move(server_latency)) {}
  TDigest client_latency;
  TDigest server_latency;
};

enum class Experiment : uint8_t {
  kUp,
  kDown,
};

template <typename Sink>
void AbslStringify(Sink& sink, Experiment e) {
  switch (e) {
    case Experiment::kUp:
      sink.Append("Up");
      break;
    case Experiment::kDown:
      sink.Append("Down");
      break;
  }
}

enum class ExperimentResult : uint8_t {
  kSuccess,
  kFailure,
  kInconclusive,
};

template <typename Sink>
void AbslStringify(Sink& sink, ExperimentResult e) {
  switch (e) {
    case ExperimentResult::kSuccess:
      sink.Append("Success");
      break;
    case ExperimentResult::kFailure:
      sink.Append("Failure");
      break;
    case ExperimentResult::kInconclusive:
      sink.Append("Inconclusive");
      break;
  }
}

ExperimentResult EvaluateExperiment(Metrics& before, Metrics& after);
ExperimentResult EvaluateQuantile(TDigest& before, TDigest& after,
                                  double quantile, double range);
ExperimentResult EvaluateOneSidedExperiment(TDigest& before, TDigest& after);
ExperimentResult MergeExperimentResults(ExperimentResult a, ExperimentResult b);
uint32_t ChooseWorstTailLatency(
    absl::flat_hash_map<uint32_t, Metrics> latencies);
Experiment Reverse(Experiment e);

}  // namespace autoscaler_detail

class AutoScaler : public RefCounted<AutoScaler> {
 public:
  using Metrics = autoscaler_detail::Metrics;

  class SubjectInterface {
   public:
    virtual ~SubjectInterface() = default;
    virtual Promise<uint32_t> AddConnection() = 0;
    virtual Promise<Empty> RemoveConnection(uint32_t which) = 0;
    virtual Promise<Empty> ParkConnection(uint32_t which) = 0;
    virtual Promise<Empty> UnparkConnection(uint32_t which) = 0;
    virtual Promise<Metrics> MeasureOverallLatency() = 0;
    virtual Promise<absl::flat_hash_map<uint32_t, Metrics>>
    MeasurePerConnectionLatency() = 0;
    virtual size_t GetNumConnections() = 0;
  };

  class Options {
   public:
  };

  AutoScaler(std::unique_ptr<SubjectInterface> subject, Options)
      : subject_(std::move(subject)) {}

  Promise<Empty> ControlLoop();

 private:
  using Experiment = autoscaler_detail::Experiment;
  using ExperimentResult = autoscaler_detail::ExperimentResult;

  enum class History : uint8_t {
    kNoHistory,
    kSuccess,
    kSuccessThenInconclusive,
  };

  struct ActiveExperiment {
    Metrics latency_before;
    size_t affected_connection;
  };

  void FinishExperiment(ExperimentResult result);
  void ReverseExperiment() {
    next_experiment_ = autoscaler_detail::Reverse(next_experiment_);
  }
  void IncreaseInterExperimentSleep() { inter_experiment_sleep_ *= 2; }
  void ResetInterExperimentSleep() {
    inter_experiment_sleep_ = Duration::Milliseconds(100);
  }

  Promise<uint32_t> Enact(Experiment e);
  Promise<Empty> Commit(Experiment e, uint32_t connection);
  Promise<Empty> Retract(Experiment e, uint32_t connection);

  Promise<ExperimentResult> PerformExperiment(Experiment direction);
  Promise<uint32_t> ParkWorstConnection();

  History history_ = History::kNoHistory;
  Experiment next_experiment_ = Experiment::kUp;
  Duration inter_experiment_sleep_ = Duration::Seconds(1);
  Duration post_enactment_sleep_ = Duration::Seconds(1);
  std::unique_ptr<ActiveExperiment> active_experiment_;
  const std::unique_ptr<SubjectInterface> subject_;
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif
