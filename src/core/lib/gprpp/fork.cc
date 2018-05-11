/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/fork.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"

/*
 * NOTE: FORKING IS NOT GENERALLY SUPPORTED, THIS IS ONLY INTENDED TO WORK
 *       AROUND VERY SPECIFIC USE CASES.
 */

namespace grpc_core {
namespace internal {
// The exec_ctx_count has 2 modes, blocked and unblocked.
// When unblocked, the count is 2-indexed; exec_ctx_count=2 indicates
// 0 active ExecCtxs, exex_ctx_count=3 indicates 1 active ExecCtxs...

// When blocked, the exec_ctx_count is 0-indexed.  Note that ExecCtx
// creation can only be blocked if there is exactly 1 outstanding ExecCtx,
// meaning that BLOCKED and UNBLOCKED counts partition the integers
#define UNBLOCKED(n) (n + 2)
#define BLOCKED(n) (n)

class ExecCtxState {
 public:
  ExecCtxState() : fork_complete_(true) {
    gpr_mu_init(&mu_);
    gpr_cv_init(&cv_);
    gpr_atm_no_barrier_store(&count_, UNBLOCKED(0));
  }

  void IncExecCtxCount() {
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

  void DecExecCtxCount() { gpr_atm_no_barrier_fetch_add(&count_, -1); }

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
  if (!overrideEnabled_) {
#ifdef GRPC_ENABLE_FORK_SUPPORT
    supportEnabled_ = true;
#else
    supportEnabled_ = false;
#endif
    bool env_var_set = false;
    char* env = gpr_getenv("GRPC_ENABLE_FORK_SUPPORT");
    if (env != nullptr) {
      static const char* truthy[] = {"yes",  "Yes",  "YES", "true",
                                     "True", "TRUE", "1"};
      static const char* falsey[] = {"no",    "No",    "NO", "false",
                                     "False", "FALSE", "0"};
      for (size_t i = 0; i < GPR_ARRAY_SIZE(truthy); i++) {
        if (0 == strcmp(env, truthy[i])) {
          supportEnabled_ = true;
          env_var_set = true;
          break;
        }
      }
      if (!env_var_set) {
        for (size_t i = 0; i < GPR_ARRAY_SIZE(falsey); i++) {
          if (0 == strcmp(env, falsey[i])) {
            supportEnabled_ = false;
            env_var_set = true;
            break;
          }
        }
      }
      gpr_free(env);
    }
  }
  if (supportEnabled_) {
    execCtxState_ = grpc_core::New<internal::ExecCtxState>();
    threadState_ = grpc_core::New<internal::ThreadState>();
  }
}

void Fork::GlobalShutdown() {
  if (supportEnabled_) {
    grpc_core::Delete(execCtxState_);
    grpc_core::Delete(threadState_);
  }
}

bool Fork::Enabled() { return supportEnabled_; }

// Testing Only
void Fork::Enable(bool enable) {
  overrideEnabled_ = true;
  supportEnabled_ = enable;
}

void Fork::IncExecCtxCount() {
  if (supportEnabled_) {
    execCtxState_->IncExecCtxCount();
  }
}

void Fork::DecExecCtxCount() {
  if (supportEnabled_) {
    execCtxState_->DecExecCtxCount();
  }
}

bool Fork::BlockExecCtx() {
  if (supportEnabled_) {
    return execCtxState_->BlockExecCtx();
  }
  return false;
}

void Fork::AllowExecCtx() {
  if (supportEnabled_) {
    execCtxState_->AllowExecCtx();
  }
}

void Fork::IncThreadCount() {
  if (supportEnabled_) {
    threadState_->IncThreadCount();
  }
}

void Fork::DecThreadCount() {
  if (supportEnabled_) {
    threadState_->DecThreadCount();
  }
}
void Fork::AwaitThreads() {
  if (supportEnabled_) {
    threadState_->AwaitThreads();
  }
}

internal::ExecCtxState* Fork::execCtxState_ = nullptr;
internal::ThreadState* Fork::threadState_ = nullptr;
bool Fork::supportEnabled_ = false;
bool Fork::overrideEnabled_ = false;

}  // namespace grpc_core
