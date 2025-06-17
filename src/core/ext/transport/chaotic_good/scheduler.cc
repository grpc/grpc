// Copyright 2025 gRPC authors.
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

#include "src/core/ext/transport/chaotic_good/scheduler.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "src/core/ext/transport/chaotic_good/tcp_ztrace_collector.h"
#include "src/core/util/shared_bit_gen.h"

namespace grpc_core::chaotic_good {

namespace {

// Helper for parsing config data.
// The config data is a sequence of key=value pairs, separated by colons.
// The value may be a string, or a double, or a boolean.
// The value may be a list of enum values, separated by commas.
// The enum values are matched against the value as a string.
// If the value matches an enum value, the enum value is assigned to the output
// variable.
// If the value does not match any enum value, the config data is considered
// invalid.
class ParseConfig {
 public:
  ParseConfig(absl::string_view name, absl::string_view value)
      : name_(name), value_(value) {}

  ParseConfig& Var(absl::string_view name, double& value) {
    if (parsed_ || name != name_) return *this;
    parsed_ = absl::SimpleAtod(value_, &value);
    return *this;
  }

  template <typename T>
  ParseConfig& Var(
      absl::string_view name, T& value,
      std::initializer_list<std::pair<absl::string_view, T>> enum_values) {
    if (parsed_ || name != name_) return *this;
    for (const auto& [enum_name, enum_value] : enum_values) {
      if (enum_name == value_) {
        parsed_ = true;
        value = enum_value;
        return *this;
      }
    }
    return *this;
  }

  void Check() {
    if (!parsed_) {
      LOG(ERROR) << "Failed to parse " << name_ << "=" << value_;
    }
  }

  bool parsed() const { return parsed_; }

 private:
  bool parsed_ = false;
  absl::string_view name_;
  absl::string_view value_;
};

// SimpleScheduler is a base class for schedulers that don't need to calculate
// a plan prior to allocating messages.
// This class takes care of the book-keeping and allows subclasses to just
// override ChooseChannel.
class SimpleScheduler : public Scheduler {
 public:
  void NewStep(double, double) override { channels_.clear(); }

  void SetConfig(absl::string_view name, absl::string_view value) override {
    LOG(ERROR) << "SimpleScheduler::SetConfig: " << name << "=" << value;
  }

  void AddChannel(uint32_t id, bool ready, double start_time,
                  double bytes_per_second) override {
    channels_.emplace_back(Channel{id, ready, start_time, bytes_per_second});
  }

  void MakePlan(TcpZTraceCollector&) override {
    num_ready_ = std::partition(channels_.begin(), channels_.end(),
                                [](const Channel& a) { return a.ready; }) -
                 channels_.begin();
  }

  std::optional<uint32_t> AllocateMessage(uint64_t bytes) override {
    const Channel* c = ChooseChannel(bytes);
    if (c == nullptr || !c->ready) return std::nullopt;
    return c->id;
  }

 protected:
  struct Channel {
    uint32_t id;
    bool ready;
    double start_time;
    double bytes_per_second;
  };

  virtual const Channel* ChooseChannel(uint64_t bytes) = 0;

  absl::Span<const Channel> channels() const { return channels_; }
  absl::Span<const Channel> ready_channels() const {
    return absl::MakeSpan(channels_).subspan(0, num_ready_);
  }
  absl::Span<const Channel> busy_channels() const {
    return absl::MakeSpan(channels_).subspan(num_ready_);
  }
  const Channel* channel(size_t i) const { return &channels_[i]; }
  size_t num_ready() const { return num_ready_; }
  size_t num_channels() const { return channels_.size(); }

  std::string BaseConfig() const { return ""; }

 private:
  size_t num_ready_;
  std::vector<Channel> channels_;
};

// Choose a random channel from the given list of channels.
// The weight function is (const Channel*, uint64_t bytes) -> double.
// The returned value is used to weight the channels against the dice roll.
// If the returned weight is zero or negative, the channel is not chosen.
// WeightFn is called multiple times for each channel, and must be
// deterministic.
template <typename Channel, typename WeightFn>
const Channel* RandomChannel(absl::Span<const Channel> channels, uint64_t bytes,
                             WeightFn weight_fn) {
  if (channels.size() == 0) return nullptr;
  if (channels.size() == 1) return &channels[0];
  double total_weight = 0.0;
  for (size_t i = 0; i < channels.size(); ++i) {
    const double weight = weight_fn(&channels[i], bytes);
    if (weight <= 0.0) continue;
    total_weight += weight;
  }
  double dice_roll = absl::Uniform(SharedBitGen(), 0.0, total_weight);
  for (size_t i = 0; i < channels.size(); ++i) {
    const Channel* c = &channels[i];
    const double weight = weight_fn(c, bytes);
    if (weight <= 0.0) continue;
    if (weight >= dice_roll) return c;
    dice_roll -= weight;
  }
  return nullptr;
}

// RandomChoiceScheduler is a scheduler that chooses a channel at random,
// weighted by a function of the channel's state.
// It's name is "rand" and takes a single parameter "weight" which is one of:
//   any_ready - choose a random ready channel
//   inverse_receive_time - choose a random channel weighted by the inverse of
//     its receive time
//   ready_inverse_receive_time - choose a random ready channel weighted by
//     the inverse of its receive time
class RandomChoiceScheduler final : public SimpleScheduler {
 public:
  void SetConfig(absl::string_view name, absl::string_view value) override {
    ParseConfig(name, value)
        .Var("weight", weight_fn_,
             {{"any_ready", WeightFn::kAnyReady},
              {"inverse_receive_time", WeightFn::kInverseReceiveTime},
              {"ready_inverse_receive_time",
               WeightFn::kReadyInverseReceiveTime}})
        .Check();
  }

  std::string Config() const override {
    return absl::StrCat("rand:weight=", weight_fn_, BaseConfig());
  }

 private:
  enum class WeightFn {
    kAnyReady,
    kInverseReceiveTime,
    kReadyInverseReceiveTime,
  };

  template <typename Sink>
  friend void AbslStringify(Sink& sink, WeightFn weight_fn) {
    switch (weight_fn) {
      case WeightFn::kAnyReady:
        sink.Append("any_ready");
        break;
      case WeightFn::kInverseReceiveTime:
        sink.Append("inverse_receive_time");
        break;
      case WeightFn::kReadyInverseReceiveTime:
        sink.Append("ready_inverse_receive_time");
        break;
    }
  }

  WeightFn weight_fn_ = WeightFn::kAnyReady;

  const Channel* ChooseChannel(uint64_t bytes) override {
    switch (weight_fn_) {
      case WeightFn::kAnyReady:
        return RandomChannel(ready_channels(), bytes,
                             [](const Channel*, uint64_t) { return 1.0; });
      case WeightFn::kInverseReceiveTime:
        return RandomChannel(
            channels(), bytes, [](const Channel* c, uint64_t bytes) {
              return 1.0 / (c->start_time + bytes / c->bytes_per_second);
            });
      case WeightFn::kReadyInverseReceiveTime:
        return RandomChannel(
            ready_channels(), bytes, [](const Channel* c, uint64_t bytes) {
              return 1.0 / (c->start_time + bytes / c->bytes_per_second);
            });
    }
    return nullptr;
  }
};

// SpanScheduler is a scheduler that calculates a plan for the outstanding work
// in a single step.
// We consider when each channel will be able to deliver its next queued byte,
// and the relative delivery rates of each channel.
// As we expect channels to become ready to send we include them in the sending
// plan.
// As we're asked to allocate messages against this plan we distribute the bytes
// to channels that have sufficient allocated space in the plan to get the
// message delivered before the overall plan end time.
// This has the nice property of not needing to particularly worry about best
// placement when there's lots of work available, and focussing down to specific
// channels only when there's a small amount of work available.
class SpanScheduler : public Scheduler {
 public:
  void NewStep(double outstanding_bytes, double min_tokens) override;

  void SetConfig(absl::string_view name, absl::string_view value) override;

  void AddChannel(uint32_t id, bool ready, double start_time,
                  double bytes_per_second) override;

  // Transition: Make a plan for the outstanding work.
  void MakePlan(TcpZTraceCollector& ztrace_collector) override;

  // Phase 2: Allocate messages against the plan.
  // If successful, returns the id of a ready channel to assign the bytes.
  // If this is not possible (all messages must go to non-ready channels),
  // returns nullopt.
  std::optional<uint32_t> AllocateMessage(uint64_t bytes) override;

 protected:
  struct Channel {
    Channel(uint32_t id, bool ready, double start_time, double bytes_per_second)
        : id(id),
          ready(ready),
          start_time(start_time),
          bytes_per_second(bytes_per_second) {}
    uint32_t id;
    bool ready;
    double start_time;
    double bytes_per_second;
    double allowed_bytes = 0.0;
  };

  absl::Span<const Channel> channels() const { return channels_; }
  absl::Span<const Channel> ready_channels() const {
    return absl::MakeSpan(channels_).subspan(0, num_ready_);
  }
  absl::Span<const Channel> busy_channels() const {
    return absl::MakeSpan(channels_).subspan(num_ready_);
  }
  const Channel* channel(size_t i) const { return &channels_[i]; }
  size_t num_ready() const { return num_ready_; }
  size_t num_channels() const { return channels_.size(); }

  std::string BaseConfig() const {
    return absl::StrCat(":step=", end_time_requested_);
  }

 private:
  void AdjustEndTimeForMinTokens();
  bool DistributeBytesToCollective(size_t max_channel_idx);
  virtual const Channel* ChooseChannel(uint64_t bytes) = 0;

  double initial_outstanding_bytes_;
  double end_time_requested_ = 1.0;
  double min_tokens_;
  double end_time_;
  double outstanding_bytes_;
  size_t num_ready_;
  std::vector<Channel> channels_;
};

void SpanScheduler::SetConfig(absl::string_view name, absl::string_view value) {
  ParseConfig(name, value).Var("step", end_time_requested_).Check();
}

void SpanScheduler::NewStep(double outstanding_bytes, double min_tokens) {
  initial_outstanding_bytes_ = outstanding_bytes;
  outstanding_bytes_ = outstanding_bytes;
  min_tokens_ = min_tokens;
  channels_.clear();
}

void SpanScheduler::AddChannel(uint32_t id, bool ready, double start_time,
                               double bytes_per_second) {
  channels_.emplace_back(id, ready, start_time, bytes_per_second);
}

void SpanScheduler::MakePlan(TcpZTraceCollector& ztrace_collector) {
  // Adjust end time to account for the min tokens.
  AdjustEndTimeForMinTokens();
  // Sort channels by their start time.
  std::sort(channels_.begin(), channels_.end(),
            [](const Channel& a, const Channel& b) {
              return a.start_time < b.start_time;
            });
  // Up until we have all channels online, we distribute work amongst the ready
  // channels such that they all finish at the next start time.
  for (size_t i = 0; i < channels_.size(); ++i) {
    if (!DistributeBytesToCollective(i)) break;
  }
  // Finally we partition channels into two groups: channels that are ready,
  // and those that are not.
  num_ready_ = std::partition(channels_.begin(), channels_.end(),
                              [](const Channel& a) { return a.ready; }) -
               channels_.begin();
  if (num_ready_ > 1) {
    std::shuffle(channels_.begin(), channels_.begin() + num_ready_,
                 SharedBitGen());
  }

  if (num_ready_ != 0) {
    ztrace_collector.Append([this]() {
      TraceWriteSchedule trace;
      trace.channels.reserve(channels_.size());
      for (const auto& channel : channels_) {
        trace.channels.push_back(TraceScheduledChannel{
            channel.id, channel.ready, channel.start_time,
            channel.bytes_per_second, channel.allowed_bytes});
      }
      std::sort(trace.channels.begin(), trace.channels.end(),
                [](const TraceScheduledChannel& a,
                   const TraceScheduledChannel& b) { return a.id < b.id; });
      trace.outstanding_bytes = initial_outstanding_bytes_;
      trace.end_time_requested = end_time_requested_;
      trace.end_time_adjusted = end_time_;
      trace.min_tokens = min_tokens_;
      trace.num_ready = num_ready_;
      return trace;
    });
  }
}

void SpanScheduler::AdjustEndTimeForMinTokens() {
  double earliest_end_time = std::numeric_limits<double>::max();
  for (Channel& channel : channels_) {
    const double end_time =
        channel.start_time + min_tokens_ / channel.bytes_per_second;
    if (end_time < earliest_end_time) {
      earliest_end_time = end_time;
    }
  }
  end_time_ = std::max(end_time_requested_, earliest_end_time);
}

bool SpanScheduler::DistributeBytesToCollective(size_t max_channel_idx) {
  if (outstanding_bytes_ < 1.0) return false;
  DCHECK_LE(max_channel_idx, channels_.size());
  // Align start times to the last channel start time.
  // (we sorted these earlier)
  const double start_time = channels_[max_channel_idx].start_time;
  if (start_time > end_time_) return false;
  // The start time of the next channel to be admitted becomes our end time
  // for this step, or if we're looking at all channels finally then the overall
  // end time is our end time for this step.
  const double end_time =
      std::min(end_time_, max_channel_idx == channels_.size() - 1
                              ? end_time_
                              : channels_[max_channel_idx + 1].start_time);
  // Calculate the total delivery rate for the collective.
  double total_delivery_rate = 0.0;
  for (size_t i = 0; i <= max_channel_idx; ++i) {
    total_delivery_rate += channels_[i].bytes_per_second;
  }
  const double bytes_deliverable =
      total_delivery_rate * (end_time - start_time);
  double bytes_to_deliver;
  if (bytes_deliverable >= outstanding_bytes_) {
    bytes_to_deliver = outstanding_bytes_;
    outstanding_bytes_ = 0.0;
  } else {
    bytes_to_deliver = bytes_deliverable;
    outstanding_bytes_ -= bytes_deliverable;
  }
  // Distribute the bytes to the channels in proportion to their delivery rate.
  for (size_t i = 0; i <= max_channel_idx; ++i) {
    channels_[i].allowed_bytes +=
        bytes_to_deliver * channels_[i].bytes_per_second / total_delivery_rate;
  }
  return true;
}

std::optional<uint32_t> SpanScheduler::AllocateMessage(uint64_t bytes) {
  if (num_ready_ == 0) return std::nullopt;
  const Channel* c = ChooseChannel(bytes);
  if (c == nullptr || c >= channels_.data() + num_ready_) return std::nullopt;
  Channel& chan = channels_[(c - channels_.data())];
  DCHECK(chan.ready);
  DCHECK_EQ(&chan, c);
  chan.allowed_bytes -= bytes;
  chan.start_time += bytes / chan.bytes_per_second;
  return c->id;
}

class SpanRoundRobinScheduler final : public SpanScheduler {
 public:
  void NewStep(double outstanding_bytes, double min_tokens) override {
    SpanScheduler::NewStep(outstanding_bytes, min_tokens);
    next_ready_ = 0;
  }

  void SetConfig(absl::string_view name, absl::string_view value) override {
    if (!ParseConfig(name, value)
             .Var("end_of_burst", end_of_burst_,
                  {{"random_delivery_time", EndOfBurst::kRandomDeliveryTime},
                   {"random_allowed_bytes", EndOfBurst::kRandomAllowedBytes},
                   {"random_ready", EndOfBurst::kRandomReady},
                   {"random_channel", EndOfBurst::kRandomChannel}})
             .parsed()) {
      SpanScheduler::SetConfig(name, value);
    }
  }

  std::string Config() const override {
    return absl::StrCat("spanrr:end_of_burst=", end_of_burst_, BaseConfig());
  }

 private:
  enum class EndOfBurst {
    kRandomDeliveryTime,
    kRandomAllowedBytes,
    kRandomReady,
    kRandomChannel,
  };

  template <typename Sink>
  friend void AbslStringify(Sink& sink, EndOfBurst e) {
    switch (e) {
      case EndOfBurst::kRandomDeliveryTime:
        sink.Append("random_delivery_time");
        break;
      case EndOfBurst::kRandomAllowedBytes:
        sink.Append("random_allowed_bytes");
        break;
      case EndOfBurst::kRandomReady:
        sink.Append("random_ready");
        break;
      case EndOfBurst::kRandomChannel:
        sink.Append("random_channel");
        break;
    }
  }

  const Channel* ChooseChannel(uint64_t bytes) override {
    DCHECK_LT(next_ready_, num_ready());
    // First search: we round robin through the ready channels, and choose the
    // first one that has space.
    const size_t first_checked = next_ready_;
    do {
      const Channel* c = channel(next_ready_);
      next_ready_ = (next_ready_ + 1) % num_ready();
      DCHECK(c->ready);
      if (c->allowed_bytes >= bytes) return c;
    } while (next_ready_ != first_checked);
    // Second search: no ready channel has capacity in this schedule to take
    // this message. Check if there's a non-ready channel that has capacity.
    // If that's the case, we're probably getting close to the end of a burst
    // and we need to get selective to ensure tail latency.
    for (size_t i = num_ready(); i < num_channels(); ++i) {
      if (channel(i)->allowed_bytes >= bytes) {
        // Yes, a non-ready channel has capacity.
        // That means we can't schedule right now.
        return channel(i);
      }
    }
    // Of course, we distributed bytes in the scheduling process, not messages.
    // And messages don't partition nicely in that view of the world... so when
    // we get here we're about at the end of a burst and we really don't have a
    // good plan for where the bytes should go.
    // Luckily(*) we've tracked the start time of the next send in the
    // scheduler, and we know the data rate of each channel - so now we just
    // choose the channel that's going to send the message soon - with some
    // randomness thrown in to de-bias the selection (light workloads need
    // this).
    switch (end_of_burst_) {
      case EndOfBurst::kRandomDeliveryTime:
        return RandomChannel(
            channels(), bytes, [](const Channel* channel, double bytes) {
              const double delivery_time =
                  channel->start_time + bytes / channel->bytes_per_second;
              return 1.0 / delivery_time;
            });
      case EndOfBurst::kRandomAllowedBytes:
        return RandomChannel(channels(), bytes,
                             [](const Channel* channel, uint64_t) {
                               return channel->allowed_bytes;
                             });
      case EndOfBurst::kRandomReady:
        return RandomChannel(ready_channels(), bytes,
                             [](const Channel*, uint64_t) { return 1.0; });
      case EndOfBurst::kRandomChannel:
        return RandomChannel(channels(), bytes,
                             [](const Channel*, uint64_t) { return 1.0; });
    }
    return nullptr;
  }

  size_t next_ready_ = 0;
  EndOfBurst end_of_burst_ = EndOfBurst::kRandomDeliveryTime;
};

}  // namespace

std::unique_ptr<Scheduler> MakeScheduler(absl::string_view config) {
  std::vector<absl::string_view> segments = absl::StrSplit(config, ':');
  auto name = segments.empty() ? "<<empty>>" : segments[0];
  std::unique_ptr<Scheduler> scheduler;
  if (name == "spanrr") {
    scheduler = std::make_unique<SpanRoundRobinScheduler>();
  } else if (name == "rand") {
    scheduler = std::make_unique<RandomChoiceScheduler>();
  } else {
    LOG(ERROR) << "Unknown scheduler type: " << name
               << " using spanrr scheduler";
    scheduler = std::make_unique<SpanRoundRobinScheduler>();
  }
  CHECK_NE(scheduler.get(), nullptr);
  for (size_t i = 1; i < segments.size(); ++i) {
    std::vector<absl::string_view> key_value = absl::StrSplit(segments[i], '=');
    switch (key_value.size()) {
      case 2:
        scheduler->SetConfig(key_value[0], key_value[1]);
        break;
      default:
        LOG(ERROR) << "Ignoring invalid scheduler config: " << segments[i];
        break;
    }
  }
  return scheduler;
}

}  // namespace grpc_core::chaotic_good
