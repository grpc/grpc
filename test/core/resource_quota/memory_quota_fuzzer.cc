// Copyright 2021 gRPC authors.
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

#include <stdint.h>
#include <sys/types.h>

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/memory_allocator.h>
#include <grpc/event_engine/memory_request.h>
#include <grpc/support/log.h>

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/resource_quota/call_checker.h"
#include "test/core/resource_quota/memory_quota_fuzzer.pb.h"
#include "test/core/util/fuzz_config_vars.h"

bool squelch = true;
bool leak_check = true;

namespace grpc_core {
namespace testing {
namespace {
ReclamationPass MapReclamationPass(memory_quota_fuzzer::Reclaimer::Pass pass) {
  switch (pass) {
    case memory_quota_fuzzer::Reclaimer::BENIGN:
      return ReclamationPass::kBenign;
    case memory_quota_fuzzer::Reclaimer::IDLE:
      return ReclamationPass::kIdle;
    case memory_quota_fuzzer::Reclaimer::DESTRUCTIVE:
      return ReclamationPass::kDestructive;
    default:
      return ReclamationPass::kBenign;
  }
}

class Fuzzer {
 public:
  void Run(const memory_quota_fuzzer::Msg& msg) {
    ExecCtx exec_ctx;
    RunMsg(msg);
    do {
      memory_quotas_.clear();
      memory_allocators_.clear();
      allocations_.clear();
      exec_ctx.Flush();
    } while (!memory_quotas_.empty() || !memory_allocators_.empty() ||
             !allocations_.empty());
  }

 private:
  void RunMsg(const memory_quota_fuzzer::Msg& msg) {
    for (int i = 0; i < msg.actions_size(); ++i) {
      const auto& action = msg.actions(i);
      switch (action.action_type_case()) {
        case memory_quota_fuzzer::Action::kFlushExecCtx:
          ExecCtx::Get()->Flush();
          break;
        case memory_quota_fuzzer::Action::kCreateQuota:
          memory_quotas_.emplace(action.quota(),
                                 MemoryQuota(absl::StrCat("quota-step-", i)));
          break;
        case memory_quota_fuzzer::Action::kDeleteQuota:
          memory_quotas_.erase(action.quota());
          break;
        case memory_quota_fuzzer::Action::kCreateAllocator:
          WithQuota(action.quota(), [this, action, i](MemoryQuota* q) {
            memory_allocators_.emplace(
                action.allocator(),
                q->CreateMemoryOwner(absl::StrCat("allocator-step-", i)));
          });
          break;
        case memory_quota_fuzzer::Action::kDeleteAllocator:
          memory_allocators_.erase(action.allocator());
          break;
        case memory_quota_fuzzer::Action::kSetQuotaSize:
          WithQuota(action.quota(), [action](MemoryQuota* q) {
            q->SetSize(Clamp(action.set_quota_size(), uint64_t{0},
                             uint64_t{std::numeric_limits<ssize_t>::max()}));
          });
          break;
        case memory_quota_fuzzer::Action::kCreateAllocation: {
          auto min = action.create_allocation().min();
          auto max = action.create_allocation().max();
          if (min > max) break;
          if (max > MemoryRequest::max_allowed_size()) break;
          MemoryRequest req(min, max);
          WithAllocator(
              action.allocator(), [this, action, req](MemoryOwner* a) {
                auto alloc = a->MakeReservation(req);
                allocations_.emplace(action.allocation(), std::move(alloc));
              });
        } break;
        case memory_quota_fuzzer::Action::kDeleteAllocation:
          allocations_.erase(action.allocation());
          break;
        case memory_quota_fuzzer::Action::kPostReclaimer: {
          std::function<void(absl::optional<ReclamationSweep>)> reclaimer;
          auto cfg = action.post_reclaimer();
          if (cfg.synchronous()) {
            reclaimer = [this, cfg](absl::optional<ReclamationSweep>) {
              RunMsg(cfg.msg());
            };
          } else {
            reclaimer = [cfg, this](absl::optional<ReclamationSweep> sweep) {
              struct Args {
                absl::optional<ReclamationSweep> sweep;
                memory_quota_fuzzer::Msg msg;
                Fuzzer* fuzzer;
              };
              auto* args = new Args{std::move(sweep), cfg.msg(), this};
              auto* closure = GRPC_CLOSURE_CREATE(
                  [](void* arg, grpc_error_handle) {
                    auto* args = static_cast<Args*>(arg);
                    args->fuzzer->RunMsg(args->msg);
                    delete args;
                  },
                  args, nullptr);
              ExecCtx::Get()->Run(DEBUG_LOCATION, closure, absl::OkStatus());
            };
            auto pass = MapReclamationPass(cfg.pass());
            WithAllocator(
                action.allocator(), [pass, reclaimer](MemoryOwner* a) {
                  // ensure called exactly once
                  auto call_checker = CallChecker::MakeOptional();
                  a->PostReclaimer(pass,
                                   [reclaimer, call_checker](
                                       absl::optional<ReclamationSweep> sweep) {
                                     call_checker->Called();
                                     reclaimer(std::move(sweep));
                                   });
                });
          }
        } break;
        case memory_quota_fuzzer::Action::ACTION_TYPE_NOT_SET:
          break;
      }
    }
  }

  template <typename F>
  void WithQuota(int quota, F f) {
    auto it = memory_quotas_.find(quota);
    if (it == memory_quotas_.end()) return;
    f(&it->second);
  }

  template <typename F>
  void WithAllocator(int allocator, F f) {
    auto it = memory_allocators_.find(allocator);
    if (it == memory_allocators_.end()) return;
    f(&it->second);
  }

  std::map<int, MemoryQuota> memory_quotas_;
  std::map<int, MemoryOwner> memory_allocators_;
  std::map<int, MemoryAllocator::Reservation> allocations_;
};

}  // namespace
}  // namespace testing
}  // namespace grpc_core

static void dont_log(gpr_log_func_args* /*args*/) {}

DEFINE_PROTO_FUZZER(const memory_quota_fuzzer::Msg& msg) {
  if (squelch) gpr_set_log_function(dont_log);
  grpc_core::ApplyFuzzConfigVars(msg.config_vars());
  grpc_core::TestOnlyReloadExperimentsFromConfigVariables();
  gpr_log_verbosity_init();
  grpc_tracer_init();
  grpc_core::testing::Fuzzer().Run(msg);
}
