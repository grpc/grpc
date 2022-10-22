//
//
// Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/fork.h"

#include <utility>

#include <grpc/support/atm.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/event_engine/thread_local.h"
#include "src/core/lib/gprpp/no_destruct.h"

//
// NOTE: FORKING IS NOT GENERALLY SUPPORTED, THIS IS ONLY INTENDED TO WORK
//       AROUND VERY SPECIFIC USE CASES.
//

namespace grpc_core {
namespace {
// The exec_ctx_count has 2 modes, blocked and unblocked.
// When unblocked, the count is 2-indexed; exec_ctx_count=2 indicates
// 0 active ExecCtxs, exex_ctx_count=3 indicates 1 active ExecCtxs...

// When blocked, the exec_ctx_count is 0-indexed.  Note that ExecCtx
// creation can only be blocked if there is exactly 1 outstanding ExecCtx,
// meaning that BLOCKED and UNBLOCKED counts partition the integers
#define UNBLOCKED(n) ((n) + 2)
#define BLOCKED(n) (n)

class ExecCtxState {
 public:
  ExecCtxState() : fork_complete_(true) {
    gpr_mu_init(&mu_);
    gpr_cv_init(&cv_);
    gpr_atm_no_barrier_store(&count_, UNBLOCKED(0));
  }

  void IncExecCtxCount() {
    // EventEngine is expected to terminate all threads before fork, and so this
    // extra work is unnecessary
    if (grpc_event_engine::experimental::ThreadLocal::IsEventEngineThread()) {
      return;
    }
    gpr_atm count = gpr_atm_no_barrier_load(&count_);
    while (true) {
      if (count <= BLOCKED(1)) {
        // This only occurs if we are trying to fork.  Wait until the fork()
        // operation completes before allowing new ExecCtxs.
        gpr_mu_lock(&mu_);
        if (gpr_atm_no_barrier_load(&count_) <= BLOCKED(1)) {
          while (!fork_complete_) {
            gpr_cv_wait(&cv_, &mu_, gpr_inf_future(GPR_CLOCK_REALTIME));
          }
        }
        gpr_mu_unlock(&mu_);
      } else if (gpr_atm_no_barrier_cas(&count_, count, count + 1)) {
        break;
      }
      count = gpr_atm_no_barrier_load(&count_);
    }
  }

  void DecExecCtxCount() {
    if (grpc_event_engine::experimental::ThreadLocal::IsEventEngineThread()) {
      return;
    }
    gpr_atm_no_barrier_fetch_add(&count_, -1);
  }

  bool BlockExecCtx() {
    // Assumes there is an active ExecCtx when this function is called
    if (gpr_atm_no_barrier_cas(&count_, UNBLOCKED(1), BLOCKED(1))) {
      gpr_mu_lock(&mu_);
      fork_complete_ = false;
      gpr_mu_unlock(&mu_);
      return true;
    }
    return false;
  }

  void AllowExecCtx() {
    gpr_mu_lock(&mu_);
    gpr_atm_no_barrier_store(&count_, UNBLOCKED(0));
    fork_complete_ = true;
    gpr_cv_broadcast(&cv_);
    gpr_mu_unlock(&mu_);
  }

  ~ExecCtxState() {
    gpr_mu_destroy(&mu_);
    gpr_cv_destroy(&cv_);
  }

 private:
  bool fork_complete_;
  gpr_mu mu_;
  gpr_cv cv_;
  gpr_atm count_;
};

class ThreadState {
 public:
  ThreadState() : awaiting_threads_(false), threads_done_(false), count_(0) {
    gpr_mu_init(&mu_);
    gpr_cv_init(&cv_);
  }

  void IncThreadCount() {
    gpr_mu_lock(&mu_);
    count_++;
    gpr_mu_unlock(&mu_);
  }

  void DecThreadCount() {
    gpr_mu_lock(&mu_);
    count_--;
    if (awaiting_threads_ && count_ == 0) {
      threads_done_ = true;
      gpr_cv_signal(&cv_);
    }
    gpr_mu_unlock(&mu_);
  }
  void AwaitThreads() {
    gpr_mu_lock(&mu_);
    awaiting_threads_ = true;
    threads_done_ = (count_ == 0);
    while (!threads_done_) {
      gpr_cv_wait(&cv_, &mu_, gpr_inf_future(GPR_CLOCK_REALTIME));
    }
    awaiting_threads_ = true;
    gpr_mu_unlock(&mu_);
  }

  ~ThreadState() {
    gpr_mu_destroy(&mu_);
    gpr_cv_destroy(&cv_);
  }

 private:
  bool awaiting_threads_;
  bool threads_done_;
  gpr_mu mu_;
  gpr_cv cv_;
  int count_;
};

}  // namespace

void Fork::GlobalInit() {
  if (!override_enabled_) {
    support_enabled_.store(ConfigVars::Get().EnableForkSupport(),
                           std::memory_order_relaxed);
  }
}

bool Fork::Enabled() {
  return support_enabled_.load(std::memory_order_relaxed);
}

void Fork::IncExecCtxCount() {
  if (GPR_UNLIKELY(support_enabled_.load(std::memory_order_relaxed))) {
    DoIncExecCtxCount();
  }
}

void Fork::DecExecCtxCount() {
  if (GPR_UNLIKELY(support_enabled_.load(std::memory_order_relaxed))) {
    DoDecExecCtxCount();
  }
}

// Testing Only
void Fork::Enable(bool enable) {
  override_enabled_ = true;
  support_enabled_.store(enable, std::memory_order_relaxed);
}

void Fork::DoIncExecCtxCount() {
  NoDestructSingleton<ExecCtxState>::Get()->IncExecCtxCount();
}

void Fork::DoDecExecCtxCount() {
  NoDestructSingleton<ExecCtxState>::Get()->DecExecCtxCount();
}

bool Fork::RegisterResetChildPollingEngineFunc(
    Fork::child_postfork_func reset_child_polling_engine) {
  if (reset_child_polling_engine_ == nullptr) {
    reset_child_polling_engine_ = new std::set<Fork::child_postfork_func>();
  }
  auto ret = reset_child_polling_engine_->insert(reset_child_polling_engine);
  return ret.second;
}

const std::set<Fork::child_postfork_func>&
Fork::GetResetChildPollingEngineFunc() {
  return *reset_child_polling_engine_;
}

bool Fork::BlockExecCtx() {
  if (support_enabled_.load(std::memory_order_relaxed)) {
    return NoDestructSingleton<ExecCtxState>::Get()->BlockExecCtx();
  }
  return false;
}

void Fork::AllowExecCtx() {
  if (support_enabled_.load(std::memory_order_relaxed)) {
    NoDestructSingleton<ExecCtxState>::Get()->AllowExecCtx();
  }
}

void Fork::IncThreadCount() {
  if (support_enabled_.load(std::memory_order_relaxed)) {
    NoDestructSingleton<ThreadState>::Get()->IncThreadCount();
  }
}

void Fork::DecThreadCount() {
  if (support_enabled_.load(std::memory_order_relaxed)) {
    NoDestructSingleton<ThreadState>::Get()->DecThreadCount();
  }
}
void Fork::AwaitThreads() {
  if (support_enabled_.load(std::memory_order_relaxed)) {
    NoDestructSingleton<ThreadState>::Get()->AwaitThreads();
  }
}

std::atomic<bool> Fork::support_enabled_(false);
bool Fork::override_enabled_ = false;
std::set<Fork::child_postfork_func>* Fork::reset_child_polling_engine_ =
    nullptr;
}  // namespace grpc_core
