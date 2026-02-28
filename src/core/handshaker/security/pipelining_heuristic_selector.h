//
//
// Copyright 2026 gRPC authors.
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
//
//

#ifndef GRPC_SRC_CORE_HANDSHAKER_SECURITY_PIPELINING_HEURISTIC_SELECTOR_H_
#define GRPC_SRC_CORE_HANDSHAKER_SECURITY_PIPELINING_HEURISTIC_SELECTOR_H_

#include <atomic>
#include <cstddef>
#include <memory>

namespace grpc_core {

class PipeliningHeuristic {
 public:
  virtual ~PipeliningHeuristic() = default;
  virtual void RecordRead(size_t read_size) = 0;
  virtual bool IsPipeliningEnabled() const = 0;
};

// Always-off heuristic.
class AlwaysOffHeuristic : public PipeliningHeuristic {
 public:
  void RecordRead(size_t /*read_size*/) override {}
  bool IsPipeliningEnabled() const override { return false; }
};

// Always-on heuristic.
class AlwaysOnHeuristic : public PipeliningHeuristic {
 public:
  void RecordRead(size_t /*read_size*/) override {}
  bool IsPipeliningEnabled() const override { return true; }
};

// Consecutive small reads heuristic.
class ConsecutiveSmallReadsHeuristic : public PipeliningHeuristic {
 public:
  void RecordRead(size_t source_buffer_length) override {
    if (source_buffer_length < kPipeliningThreshold) {
      if (enable_pipelining_) {
        if (small_read_counter_.fetch_add(1, std::memory_order_relaxed) ==
            kNumConsecutiveSmallReads) {
          enable_pipelining_ = false;
        }
      }
    } else {
      if (!enable_pipelining_) {
        enable_pipelining_ = true;
      }
      small_read_counter_.store(0, std::memory_order_relaxed);
    }
  }
  bool IsPipeliningEnabled() const override { return enable_pipelining_; }

 private:
  // Minimum number of bytes available before we re-enable pipelining of reads;
  // otherwise, we read and unprotect inline.
  static constexpr size_t kPipeliningThreshold = 950 * 1024;
  // Number of consecutive small reads before we disable pipelining, if
  // currently enabled.
  static constexpr size_t kNumConsecutiveSmallReads = 30;
  // We disable pipelining initially.
  bool enable_pipelining_ = false;
  std::atomic<size_t> small_read_counter_{0};
};

// Moving average heuristic.
class MovingAverageHeuristic : public PipeliningHeuristic {
 public:
  void RecordRead(size_t source_buffer_length) override {
    if (moving_average_ == 0.0) {
      moving_average_ = source_buffer_length;
    } else {
      moving_average_ = moving_average_ * 0.95 + source_buffer_length * 0.05;
    }
    if (moving_average_ < kPipeliningDisableThreshold && enable_pipelining_) {
      enable_pipelining_ = false;
    } else if (moving_average_ > kPipeliningEnableThreshold &&
               !enable_pipelining_) {
      enable_pipelining_ = true;
    }
  }
  bool IsPipeliningEnabled() const override { return enable_pipelining_; }

 private:
  // If moving average of reads is below this threshold, we disable pipelining.
  static constexpr size_t kPipeliningDisableThreshold = 64 * 1024;
  // If moving average of reads is above this threshold, we enable pipelining.
  static constexpr size_t kPipeliningEnableThreshold = 950 * 1024;
  // We disable pipelining initially.
  bool enable_pipelining_ = false;
  double moving_average_ = 0.0;
};

class PipeliningHeuristicSelector {
 public:
  enum class HeuristicType {
    kConsecutiveSmallReads,
    kMovingAverage,
    kAlwaysOff,
    kAlwaysOn,
  };

  // Default to kMovingAverage.
  explicit PipeliningHeuristicSelector(
      HeuristicType type = HeuristicType::kMovingAverage) {
    SetHeuristicType(type);
  }

  void SetHeuristicType(HeuristicType type) {
    switch (type) {
      case HeuristicType::kConsecutiveSmallReads:
        heuristic_ = std::make_unique<ConsecutiveSmallReadsHeuristic>();
        break;
      case HeuristicType::kMovingAverage:
        heuristic_ = std::make_unique<MovingAverageHeuristic>();
        break;
      case HeuristicType::kAlwaysOff:
        heuristic_ = std::make_unique<AlwaysOffHeuristic>();
        break;
      case HeuristicType::kAlwaysOn:
        heuristic_ = std::make_unique<AlwaysOnHeuristic>();
        break;
    }
  }

  void RecordRead(size_t source_buffer_length) {
    heuristic_->RecordRead(source_buffer_length);
  }

  bool IsPipeliningEnabled() const { return heuristic_->IsPipeliningEnabled(); }

 private:
  std::unique_ptr<PipeliningHeuristic> heuristic_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_HANDSHAKER_SECURITY_PIPELINING_HEURISTIC_SELECTOR_H_
