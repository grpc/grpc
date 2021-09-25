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

#include <map>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/memory_quota.h"
#include "src/libfuzzer/libfuzzer_macro.h"
#include "test/core/resource_quota/memory_quota_fuzzer.pb.h"

bool squelch = true;
bool leak_check = true;

namespace grpc_core {

namespace {
struct BaseThing {
  virtual ~BaseThing() {}
};

template <int N>
struct Thing : public BaseThing {
  char data[N];
};

std::unique_ptr<BaseThing> MakeThing(MemoryAllocator* a,
                                     memory_quota_fuzzer::Action::Thing size) {
#define CASE(n)                               \
  case memory_quota_fuzzer::Action::THING##n: \
    return a->MakeUnique<Thing<n>>()
  switch (size) {
    CASE(1);
    CASE(2);
    CASE(3);
    CASE(5);
    CASE(8);
    CASE(13);
    CASE(21);
    CASE(34);
    CASE(55);
    CASE(89);
    CASE(144);
    CASE(233);
    CASE(377);
    CASE(610);
    CASE(987);
    CASE(1597);
    CASE(2584);
    CASE(4181);
    CASE(6765);
    CASE(10946);
    CASE(17711);
    CASE(28657);
    CASE(46368);
    CASE(75025);
    default:
      return nullptr;
  }
}

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
    grpc_core::ExecCtx exec_ctx;
    RunMsg(msg);
    memory_quotas_.clear();
    memory_allocators_.clear();
    things_.clear();
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
                                 RefCountedPtr<MemoryQuota>(new MemoryQuota()));
          break;
        case memory_quota_fuzzer::Action::kDeleteQuota:
          memory_quotas_.erase(action.quota());
          break;
        case memory_quota_fuzzer::Action::kCreateAllocator:
          WithQuota(action.quota(),
                    [this, action](RefCountedPtr<MemoryQuota> q) {
                      memory_allocators_.emplace(action.allocator(),
                                                 q->MakeMemoryAllocator());
                    });
          break;
        case memory_quota_fuzzer::Action::kDeleteAllocator:
          memory_allocators_.erase(action.allocator());
          break;
        case memory_quota_fuzzer::Action::kSetQuotaSize:
          WithQuota(action.quota(), [action](RefCountedPtr<MemoryQuota> q) {
            q->SetSize(Clamp(action.set_quota_size(), uint64_t{0},
                             uint64_t{std::numeric_limits<ssize_t>::max()}));
          });
          break;
        case memory_quota_fuzzer::Action::kRebindQuota:
          WithQuota(action.quota(),
                    [this, action](RefCountedPtr<MemoryQuota> q) {
                      WithAllocator(action.allocator(),
                                    [q](MemoryAllocator* a) { a->Rebind(q); });
                    });
          break;
        case memory_quota_fuzzer::Action::kCreateThing:
          WithAllocator(action.allocator(), [this, action](MemoryAllocator* a) {
            things_.emplace(action.thing(),
                            MakeThing(a, action.create_thing()));
          });
          break;
        case memory_quota_fuzzer::Action::kPostReclaimer: {
          std::function<void(ReclamationSweep)> reclaimer;
          auto cfg = action.post_reclaimer();
          if (cfg.synchronous()) {
            reclaimer = [this, cfg](ReclamationSweep sweep) {
              RunMsg(cfg.msg());
            };
          } else {
            reclaimer = [cfg, this](ReclamationSweep sweep) {
              struct Args {
                ReclamationSweep sweep;
                memory_quota_fuzzer::Msg msg;
                Fuzzer* fuzzer;
              };
              auto* args = new Args{std::move(sweep), cfg.msg(), this};
              auto* closure = GRPC_CLOSURE_CREATE(
                  [](void* arg, grpc_error* error) {
                    auto* args = static_cast<Args*>(arg);
                    args->fuzzer->RunMsg(args->msg);
                    delete args;
                  },
                  args, nullptr);
              ExecCtx::Get()->Run(DEBUG_LOCATION, closure, GRPC_ERROR_NONE);
            };
            auto pass = MapReclamationPass(cfg.pass());
            WithAllocator(action.allocator(),
                          [pass, reclaimer](MemoryAllocator* a) {
                            a->PostReclaimer(pass, reclaimer);
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
    f(it->second);
  }

  template <typename F>
  void WithAllocator(int allocator, F f) {
    auto it = memory_allocators_.find(allocator);
    if (it == memory_allocators_.end()) return;
    f(it->second.get());
  }

  std::map<int, RefCountedPtr<MemoryQuota>> memory_quotas_;
  std::map<int, OrphanablePtr<MemoryAllocator>> memory_allocators_;
  std::map<int, std::unique_ptr<BaseThing>> things_;
};

}  // namespace

}  // namespace grpc_core

static void dont_log(gpr_log_func_args* /*args*/) {}

DEFINE_PROTO_FUZZER(const memory_quota_fuzzer::Msg& msg) {
  if (squelch) gpr_set_log_function(dont_log);
  gpr_log_verbosity_init();
  grpc_tracer_init();
  grpc_core::Fuzzer().Run(msg);
}
