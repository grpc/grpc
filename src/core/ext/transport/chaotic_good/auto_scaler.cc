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

#include "src/core/ext/transport/chaotic_good/auto_scaler.h"

#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/loop.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/promise/sleep.h"
#include "src/core/util/kolmogorov_smirnov.h"

namespace grpc_core {
namespace chaotic_good {

namespace autoscaler_detail {
ExperimentResult EvaluateExperiment(Metrics& latency_before,
                                    Metrics& latency_after) {
  static const double kAlpha = 0.2;
  const bool client_changed = KolmogorovSmirnovTest(
      latency_before.client_latency, latency_after.client_latency, kAlpha);
  const bool server_changed = KolmogorovSmirnovTest(
      latency_before.server_latency, latency_after.server_latency, kAlpha);
  const ExperimentResult client_result =
      client_changed ? EvaluateOneSidedExperiment(latency_before.client_latency,
                                                  latency_after.client_latency)
                     : ExperimentResult::kInconclusive;
  const ExperimentResult server_result =
      server_changed ? EvaluateOneSidedExperiment(latency_before.server_latency,
                                                  latency_after.server_latency)
                     : ExperimentResult::kInconclusive;
  return MergeExperimentResults(client_result, server_result);
}

ExperimentResult EvaluateQuantile(TDigest& before, TDigest& after,
                                  double quantile, double range) {
  const double before_lower = before.Quantile(quantile - range);
  const double before_upper = before.Quantile(quantile + range);
  const double after_value = after.Quantile(quantile);
  if (after_value < before_lower) return ExperimentResult::kSuccess;
  if (after_value > before_upper) return ExperimentResult::kFailure;
  return ExperimentResult::kInconclusive;
}

ExperimentResult EvaluateOneSidedExperiment(TDigest& before, TDigest& after) {
  return MergeExperimentResults(EvaluateQuantile(before, after, 0.5, 0.05),
                                EvaluateQuantile(before, after, 0.75, 0.05));
}

ExperimentResult MergeExperimentResults(ExperimentResult a,
                                        ExperimentResult b) {
  if (a == ExperimentResult::kInconclusive) return b;
  if (b == ExperimentResult::kInconclusive) return a;
  return a == b ? a : ExperimentResult::kInconclusive;
}

size_t ChooseWorstTailLatency(std::vector<Metrics> latencies) {
  static const double kQuantile = 0.75;
  auto measure = [](Metrics& m) {
    return std::max(m.client_latency.Quantile(kQuantile),
                    m.server_latency.Quantile(kQuantile));
  };
  CHECK(!latencies.empty());
  uint32_t worst = 0;
  double worst_latency = measure(latencies[0]);
  for (size_t i = 1; i < latencies.size(); ++i) {
    const double latency = measure(latencies[i]);
    if (latency > worst_latency) {
      worst = i;
      worst_latency = latency;
    }
  }
  return worst;
}

Experiment Reverse(Experiment e) {
  switch (e) {
    case Experiment::kUp:
      return Experiment::kDown;
    case Experiment::kDown:
      return Experiment::kUp;
  }
  GPR_UNREACHABLE_CODE(LOG(FATAL) << "unknown experiment");
}
}  // namespace autoscaler_detail

Promise<Empty> AutoScaler::ControlLoop() {
  return Loop([self = Ref()]() {
    return Seq(
        Sleep(Timestamp::Now() + self->inter_experiment_sleep_),
        [self]() { return self->PerformExperiment(self->next_experiment_); },
        [self](ExperimentResult result) -> LoopCtl<Empty> {
          self->FinishExperiment(result);
          return Continue{};
        });
  });
}

void AutoScaler::FinishExperiment(ExperimentResult result) {
  switch (result) {
    case ExperimentResult::kInconclusive:
      IncreaseInterExperimentSleep();
      switch (history_) {
        case History::kNoHistory:
          ReverseExperiment();
          break;
        case History::kSuccess:
          history_ = History::kSuccessThenInconclusive;
          break;
        case History::kSuccessThenInconclusive:
          history_ = History::kNoHistory;
          ReverseExperiment();
          break;
      }
      break;
    case ExperimentResult::kSuccess:
      history_ = History::kSuccess;
      ResetInterExperimentSleep();
      break;
    case ExperimentResult::kFailure:
      history_ = History::kNoHistory;
      ReverseExperiment();
      IncreaseInterExperimentSleep();
      break;
  }
}

Promise<uint32_t> AutoScaler::Enact(Experiment e) {
  switch (e) {
    case Experiment::kUp:
      return subject_->AddConnection();
    case Experiment::kDown:
      return ParkWorstConnection();
  }
  GPR_UNREACHABLE_CODE(return Never<uint32_t>());
}

Promise<uint32_t> AutoScaler::ParkWorstConnection() {
  return Seq(subject_->MeasurePerConnectionLatency(),
             [self = Ref()](std::vector<Metrics> latencies) {
               const uint32_t worst = autoscaler_detail::ChooseWorstTailLatency(
                   std::move(latencies));
               return Map(self->subject_->ParkConnection(worst),
                          [worst](Empty) { return worst; });
             });
}

Promise<Empty> AutoScaler::Commit(Experiment e, uint32_t connection) {
  switch (e) {
    case Experiment::kUp:
      return []() { return Empty{}; };
    case Experiment::kDown:
      return subject_->RemoveConnection(connection);
  }
  GPR_UNREACHABLE_CODE(return Immediate(Empty{}));
}

Promise<Empty> AutoScaler::Retract(Experiment e, uint32_t connection) {
  switch (e) {
    case Experiment::kUp:
      return subject_->RemoveConnection(connection);
    case Experiment::kDown:
      return subject_->UnparkConnection(connection);
  }
  GPR_UNREACHABLE_CODE(return Immediate(Empty{}));
}

Promise<AutoScaler::ExperimentResult> AutoScaler::PerformExperiment(
    Experiment direction) {
  active_experiment_ = std::make_unique<ActiveExperiment>();
  if (direction == Experiment::kDown && subject_->GetNumConnections() == 0) {
    // Skip experiment if we're already at the minimum - this can never succeed
    return Immediate(ExperimentResult::kFailure);
  }
  return Seq(
      subject_->MeasureOverallLatency(),
      [self = Ref(), direction](Metrics latency) {
        self->active_experiment_->latency_before = std::move(latency);
        return self->Enact(direction);
      },
      [self = Ref()](uint32_t connection) {
        self->active_experiment_->affected_connection = connection;
        return Sleep(Timestamp::Now() + self->post_enactment_sleep_);
      },
      [self = Ref()]() { return self->subject_->MeasureOverallLatency(); },
      [self = Ref(), direction](Metrics latency) {
        const auto result = autoscaler_detail::EvaluateExperiment(
            self->active_experiment_->latency_before, latency);
        const uint32_t connection =
            self->active_experiment_->affected_connection;
        self->active_experiment_.reset();
        return Map(If(
                       result == ExperimentResult::kSuccess,
                       [direction, connection, self]() {
                         return self->Commit(direction, connection);
                       },
                       [direction, connection, self]() {
                         return self->Retract(direction, connection);
                       }),
                   [result](Empty) { return result; });
      });
}

}  // namespace chaotic_good
}  // namespace grpc_core
