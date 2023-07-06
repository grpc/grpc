//
//
// Copyright 2015 gRPC authors.
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

#include "test/core/end2end/cq_verifier.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/byte_buffer.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>

#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/match.h"
#include "src/core/lib/surface/event_string.h"
#include "test/core/util/test_config.h"

// a set of metadata we expect to find on an event
typedef struct metadata {
  size_t count;
  size_t cap;
  char** keys;
  char** values;
} metadata;

static int has_metadata(const grpc_metadata* md, size_t count, const char* key,
                        const char* value) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (0 == grpc_slice_str_cmp(md[i].key, key) &&
        0 == grpc_slice_str_cmp(md[i].value, value)) {
      return 1;
    }
  }
  return 0;
}

int contains_metadata(grpc_metadata_array* array, const char* key,
                      const char* value) {
  return has_metadata(array->metadata, array->count, key, value);
}

static int has_metadata_slices(const grpc_metadata* md, size_t count,
                               grpc_slice key, grpc_slice value) {
  size_t i;
  for (i = 0; i < count; i++) {
    if (grpc_slice_eq(md[i].key, key) && grpc_slice_eq(md[i].value, value)) {
      return 1;
    }
  }
  return 0;
}

int contains_metadata_slices(grpc_metadata_array* array, grpc_slice key,
                             grpc_slice value) {
  return has_metadata_slices(array->metadata, array->count, key, value);
}

static grpc_slice merge_slices(grpc_slice* slices, size_t nslices) {
  size_t i;
  size_t len = 0;
  uint8_t* cursor;
  grpc_slice out;

  for (i = 0; i < nslices; i++) {
    len += GRPC_SLICE_LENGTH(slices[i]);
  }

  out = grpc_slice_malloc(len);
  cursor = GRPC_SLICE_START_PTR(out);

  for (i = 0; i < nslices; i++) {
    memcpy(cursor, GRPC_SLICE_START_PTR(slices[i]),
           GRPC_SLICE_LENGTH(slices[i]));
    cursor += GRPC_SLICE_LENGTH(slices[i]);
  }

  return out;
}

int raw_byte_buffer_eq_slice(grpc_byte_buffer* rbb, grpc_slice b) {
  grpc_slice a;
  int ok;

  if (!rbb) return 0;

  a = merge_slices(rbb->data.raw.slice_buffer.slices,
                   rbb->data.raw.slice_buffer.count);
  ok = GRPC_SLICE_LENGTH(a) == GRPC_SLICE_LENGTH(b) &&
       0 == memcmp(GRPC_SLICE_START_PTR(a), GRPC_SLICE_START_PTR(b),
                   GRPC_SLICE_LENGTH(a));
  if (!ok) {
    gpr_log(GPR_ERROR,
            "SLICE MISMATCH: left_length=%" PRIuPTR " right_length=%" PRIuPTR,
            GRPC_SLICE_LENGTH(a), GRPC_SLICE_LENGTH(b));
    std::string out;
    const char* a_str = reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(a));
    const char* b_str = reinterpret_cast<const char*>(GRPC_SLICE_START_PTR(b));
    for (size_t i = 0; i < std::max(GRPC_SLICE_LENGTH(a), GRPC_SLICE_LENGTH(b));
         i++) {
      if (i >= GRPC_SLICE_LENGTH(a)) {
        absl::StrAppend(&out, "\u001b[36m",  // cyan
                        absl::CEscape(absl::string_view(&b_str[i], 1)),
                        "\u001b[0m");
      } else if (i >= GRPC_SLICE_LENGTH(b)) {
        absl::StrAppend(&out, "\u001b[35m",  // magenta
                        absl::CEscape(absl::string_view(&a_str[i], 1)),
                        "\u001b[0m");
      } else if (a_str[i] == b_str[i]) {
        absl::StrAppend(&out, absl::CEscape(absl::string_view(&a_str[i], 1)));
      } else {
        absl::StrAppend(&out, "\u001b[31m",  // red
                        absl::CEscape(absl::string_view(&a_str[i], 1)),
                        "\u001b[33m",  // yellow
                        absl::CEscape(absl::string_view(&b_str[i], 1)),
                        "\u001b[0m");
      }
      gpr_log(GPR_ERROR, "%s", out.c_str());
    }
  }
  grpc_slice_unref(a);
  grpc_slice_unref(b);
  return ok;
}

int byte_buffer_eq_slice(grpc_byte_buffer* bb, grpc_slice b) {
  if (bb == nullptr) return 0;
  if (bb->data.raw.compression > GRPC_COMPRESS_NONE) {
    grpc_slice_buffer decompressed_buffer;
    grpc_slice_buffer_init(&decompressed_buffer);
    GPR_ASSERT(grpc_msg_decompress(bb->data.raw.compression,
                                   &bb->data.raw.slice_buffer,
                                   &decompressed_buffer));
    grpc_byte_buffer* rbb = grpc_raw_byte_buffer_create(
        decompressed_buffer.slices, decompressed_buffer.count);
    int ret_val = raw_byte_buffer_eq_slice(rbb, b);
    grpc_byte_buffer_destroy(rbb);
    grpc_slice_buffer_destroy(&decompressed_buffer);
    return ret_val;
  }
  return raw_byte_buffer_eq_slice(bb, b);
}

int byte_buffer_eq_string(grpc_byte_buffer* bb, const char* str) {
  return byte_buffer_eq_slice(bb, grpc_slice_from_copied_string(str));
}

namespace {
bool IsProbablyInteger(void* p) {
  return reinterpret_cast<uintptr_t>(p) < 1000000 ||
         (reinterpret_cast<uintptr_t>(p) >
          std::numeric_limits<uintptr_t>::max() - 10);
}

std::string TagStr(void* tag) {
  if (IsProbablyInteger(tag)) {
    return absl::StrFormat("tag(%" PRIdPTR ")",
                           reinterpret_cast<intptr_t>(tag));
  } else {
    return absl::StrFormat("%p", tag);
  }
}
}  // namespace

namespace grpc_core {

CqVerifier::CqVerifier(
    grpc_completion_queue* cq, absl::AnyInvocable<void(Failure) const> fail,
    absl::AnyInvocable<
        void(grpc_event_engine::experimental::EventEngine::Duration) const>
        step_fn)
    : cq_(cq), fail_(std::move(fail)), step_fn_(std::move(step_fn)) {}

CqVerifier::~CqVerifier() { Verify(); }

std::string CqVerifier::Expectation::ToString() const {
  return absl::StrCat(
      location.file(), ":", location.line(), ": ", TagStr(tag), " ",
      Match(
          result,
          [](bool success) {
            return absl::StrCat("success=", success ? "true" : "false");
          },
          [](Maybe) { return std::string("maybe"); },
          [](AnyStatus) { return std::string("any success value"); },
          [](const PerformAction&) {
            return std::string("perform some action");
          },
          [](const MaybePerformAction&) {
            return std::string("maybe perform action");
          }));
}

std::string CqVerifier::Expectation::ToShortString() const {
  return absl::StrCat(
      TagStr(tag),
      Match(
          result,
          [](bool success) -> std::string {
            if (!success) return "-❌";
            return "-✅";
          },
          [](Maybe) { return std::string("-❓"); },
          [](AnyStatus) { return std::string("-🤷"); },
          [](const PerformAction&) { return std::string("-🎬"); },
          [](const MaybePerformAction&) { return std::string("-🎬❓"); }));
}

std::vector<std::string> CqVerifier::ToStrings() const {
  std::vector<std::string> expectations;
  expectations.reserve(expectations_.size());
  for (const auto& e : expectations_) {
    expectations.push_back(e.ToString());
  }
  return expectations;
}

std::string CqVerifier::ToString() const {
  return absl::StrJoin(ToStrings(), "\n");
}

std::vector<std::string> CqVerifier::ToShortStrings() const {
  std::vector<std::string> expectations;
  expectations.reserve(expectations_.size());
  for (const auto& e : expectations_) {
    expectations.push_back(e.ToShortString());
  }
  return expectations;
}

std::string CqVerifier::ToShortString() const {
  return absl::StrJoin(ToShortStrings(), " ");
}

void CqVerifier::FailNoEventReceived(const SourceLocation& location) const {
  fail_(Failure{location, "No event received", ToStrings(), {}});
}

void CqVerifier::FailUnexpectedEvent(grpc_event* ev,
                                     const SourceLocation& location) const {
  std::vector<std::string> message_details;
  if (ev->type == GRPC_OP_COMPLETE && ev->success) {
    auto successful_state_strings = successful_state_strings_.find(ev->tag);
    if (successful_state_strings != successful_state_strings_.end()) {
      for (SuccessfulStateString* sss : successful_state_strings->second) {
        message_details.emplace_back(sss->GetSuccessfulStateString());
      }
    }
  }
  fail_(Failure{location,
                absl::StrCat("Unexpected event: ", grpc_event_string(ev)),
                ToStrings(), std::move(message_details)});
}

namespace {
std::string CrashMessage(const CqVerifier::Failure& failure) {
  std::string message = failure.message;
  if (!failure.message_details.empty()) {
    absl::StrAppend(&message, "\nwith:");
    for (const auto& detail : failure.message_details) {
      absl::StrAppend(&message, "\n  ", detail);
    }
  }
  absl::StrAppend(&message, "\nchecked @ ", failure.location.file(), ":",
                  failure.location.line());
  if (!failure.expected.empty()) {
    absl::StrAppend(&message, "\nexpected:\n");
    for (const auto& line : failure.expected) {
      absl::StrAppend(&message, "  ", line, "\n");
    }
  } else {
    absl::StrAppend(&message, "\nexpected nothing");
  }
  return message;
}
}  // namespace

void CqVerifier::FailUsingGprCrashWithStdio(const Failure& failure) {
  CrashWithStdio(CrashMessage(failure));
}

void CqVerifier::FailUsingGprCrash(const Failure& failure) {
  Crash(CrashMessage(failure));
}

void CqVerifier::FailUsingGtestFail(const Failure& failure) {
  std::string message = absl::StrCat("  ", failure.message);
  if (!failure.expected.empty()) {
    absl::StrAppend(&message, "\n  expected:\n");
    for (const auto& line : failure.expected) {
      absl::StrAppend(&message, "    ", line, "\n");
    }
  } else {
    absl::StrAppend(&message, "\n  expected nothing");
  }
  ADD_FAILURE_AT(failure.location.file(), failure.location.line()) << message;
}

namespace {
bool IsMaybe(const CqVerifier::ExpectedResult& r) {
  return Match(
      r, [](bool) { return false; }, [](CqVerifier::Maybe) { return true; },
      [](CqVerifier::AnyStatus) { return false; },
      [](const CqVerifier::PerformAction&) { return false; },
      [](const CqVerifier::MaybePerformAction&) { return true; });
}
}  // namespace

grpc_event CqVerifier::Step(gpr_timespec deadline) {
  if (step_fn_ != nullptr) {
    while (true) {
      grpc_event r = grpc_completion_queue_next(
          cq_, gpr_inf_past(deadline.clock_type), nullptr);
      if (r.type != GRPC_QUEUE_TIMEOUT) return r;
      auto now = gpr_now(deadline.clock_type);
      if (gpr_time_cmp(deadline, now) < 0) break;
      step_fn_(Timestamp::FromTimespecRoundDown(deadline) - Timestamp::Now());
    }
    return grpc_event{GRPC_QUEUE_TIMEOUT, 0, nullptr};
  }
  return grpc_completion_queue_next(cq_, deadline, nullptr);
}

void CqVerifier::Verify(Duration timeout, SourceLocation location) {
  if (expectations_.empty()) return;
  if (log_verifications_) {
    gpr_log(GPR_ERROR, "Verify %s for %s", ToShortString().c_str(),
            timeout.ToString().c_str());
  }
  const gpr_timespec deadline =
      grpc_timeout_milliseconds_to_deadline(timeout.millis());
  while (!expectations_.empty()) {
    grpc_event ev = Step(deadline);
    if (ev.type == GRPC_QUEUE_TIMEOUT) break;
    if (ev.type != GRPC_OP_COMPLETE) {
      FailUnexpectedEvent(&ev, location);
    }
    bool found = false;
    for (auto it = expectations_.begin(); it != expectations_.end(); ++it) {
      if (it->tag != ev.tag) continue;
      auto expectation = std::move(*it);
      expectations_.erase(it);
      const bool expected = Match(
          expectation.result,
          [ev](bool success) { return ev.success == success; },
          [ev](Maybe m) {
            if (m.seen != nullptr) *m.seen = true;
            return ev.success != 0;
          },
          [ev](AnyStatus a) {
            if (a.result != nullptr) *a.result = ev.success;
            return true;
          },
          [ev](const PerformAction& action) {
            action.action(ev.success);
            return true;
          },
          [ev](const MaybePerformAction& action) {
            action.action(ev.success);
            return true;
          });
      if (!expected) {
        FailUnexpectedEvent(&ev, location);
      }
      found = true;
      break;
    }
    if (!found) FailUnexpectedEvent(&ev, location);
    if (AllMaybes()) break;
  }
  expectations_.erase(
      std::remove_if(expectations_.begin(), expectations_.end(),
                     [](const Expectation& e) { return IsMaybe(e.result); }),
      expectations_.end());
  if (!expectations_.empty()) FailNoEventReceived(location);
}

bool CqVerifier::AllMaybes() const {
  for (const auto& e : expectations_) {
    if (!IsMaybe(e.result)) return false;
  }
  return true;
}

void CqVerifier::VerifyEmpty(Duration timeout, SourceLocation location) {
  if (log_verifications_) {
    gpr_log(GPR_ERROR, "Verify empty completion queue for %s",
            timeout.ToString().c_str());
  }
  const gpr_timespec deadline =
      gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), timeout.as_timespec());
  GPR_ASSERT(expectations_.empty());
  grpc_event ev = Step(deadline);
  if (ev.type != GRPC_QUEUE_TIMEOUT) {
    FailUnexpectedEvent(&ev, location);
  }
}

void CqVerifier::Expect(void* tag, ExpectedResult result,
                        SourceLocation location) {
  expectations_.push_back(Expectation{location, tag, std::move(result)});
}

void CqVerifier::AddSuccessfulStateString(
    void* tag, SuccessfulStateString* successful_state_string) {
  successful_state_strings_[tag].push_back(successful_state_string);
}

void CqVerifier::ClearSuccessfulStateStrings(void* tag) {
  successful_state_strings_.erase(tag);
}

}  // namespace grpc_core
