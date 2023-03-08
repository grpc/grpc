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

#include <vector>

#include "absl/base/thread_annotations.h"

#include <grpc/grpc.h>
#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/time.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/impl/completion_queue_tag.h>
#include <grpcpp/impl/grpc_library.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/gprpp/thd.h"

namespace grpc {
namespace {

gpr_once g_once_init_callback_alternative = GPR_ONCE_INIT;
grpc_core::Mutex* g_callback_alternative_mu;

// Implement a ref-counted callback CQ for global use in the alternative
// implementation so that its threads are only created once. Do this using
// explicit ref-counts and raw pointers rather than a shared-ptr since that
// has a non-trivial destructor and thus can't be used for global variables.
struct CallbackAlternativeCQ {
  int refs ABSL_GUARDED_BY(g_callback_alternative_mu) = 0;
  CompletionQueue* cq ABSL_GUARDED_BY(g_callback_alternative_mu);
  std::vector<grpc_core::Thread>* nexting_threads
      ABSL_GUARDED_BY(g_callback_alternative_mu);

  CompletionQueue* Ref() {
    grpc_core::MutexLock lock(&*g_callback_alternative_mu);
    refs++;
    if (refs == 1) {
      cq = new CompletionQueue;
      int num_nexting_threads =
          grpc_core::Clamp(gpr_cpu_num_cores() / 2, 2u, 16u);
      nexting_threads = new std::vector<grpc_core::Thread>;
      for (int i = 0; i < num_nexting_threads; i++) {
        nexting_threads->emplace_back(
            "nexting_thread",
            [](void* arg) {
              grpc_completion_queue* cq =
                  static_cast<CompletionQueue*>(arg)->cq();
              while (true) {
                // Use the raw Core next function rather than the C++ Next since
                // Next incorporates FinalizeResult and we actually want that
                // called from the callback functor itself.
                // TODO(vjpai): Migrate below to next without a timeout or idle
                // phase. That's currently starving out some other polling,
                // though.
                auto ev = grpc_completion_queue_next(
                    cq,
                    gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                 gpr_time_from_millis(1000, GPR_TIMESPAN)),
                    nullptr);
                if (ev.type == GRPC_QUEUE_SHUTDOWN) {
                  return;
                }
                if (ev.type == GRPC_QUEUE_TIMEOUT) {
                  gpr_sleep_until(
                      gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                                   gpr_time_from_millis(100, GPR_TIMESPAN)));
                  continue;
                }
                GPR_DEBUG_ASSERT(ev.type == GRPC_OP_COMPLETE);
                // We can always execute the callback inline rather than
                // pushing it to another Executor thread because this
                // thread is definitely running on a background thread, does not
                // hold any application locks before executing the callback,
                // and cannot be entered recursively.
                auto* functor =
                    static_cast<grpc_completion_queue_functor*>(ev.tag);
                functor->functor_run(functor, ev.success);
              }
            },
            cq);
      }
      for (auto& th : *nexting_threads) {
        th.Start();
      }
    }
    return cq;
  }

  void Unref() {
    grpc_core::MutexLock lock(g_callback_alternative_mu);
    refs--;
    if (refs == 0) {
      cq->Shutdown();
      for (auto& th : *nexting_threads) {
        th.Join();
      }
      delete nexting_threads;
      delete cq;
    }
  }
};

CallbackAlternativeCQ g_callback_alternative_cq;

}  // namespace

// 'CompletionQueue' constructor can safely call GrpcLibraryCodegen(false) here
// i.e not have GrpcLibraryCodegen call grpc_init(). This is because, to create
// a 'grpc_completion_queue' instance (which is being passed as the input to
// this constructor), one must have already called grpc_init().
CompletionQueue::CompletionQueue(grpc_completion_queue* take)
    : GrpcLibrary(false), cq_(take) {
  InitialAvalanching();
}

void CompletionQueue::Shutdown() {
#ifndef NDEBUG
  if (!ServerListEmpty()) {
    gpr_log(GPR_ERROR,
            "CompletionQueue shutdown being shutdown before its server.");
  }
#endif
  CompleteAvalanching();
}

CompletionQueue::NextStatus CompletionQueue::AsyncNextInternal(
    void** tag, bool* ok, gpr_timespec deadline) {
  for (;;) {
    auto ev = grpc_completion_queue_next(cq_, deadline, nullptr);
    switch (ev.type) {
      case GRPC_QUEUE_TIMEOUT:
        return TIMEOUT;
      case GRPC_QUEUE_SHUTDOWN:
        return SHUTDOWN;
      case GRPC_OP_COMPLETE:
        auto core_cq_tag =
            static_cast<grpc::internal::CompletionQueueTag*>(ev.tag);
        *ok = ev.success != 0;
        *tag = core_cq_tag;
        if (core_cq_tag->FinalizeResult(tag, ok)) {
          return GOT_EVENT;
        }
        break;
    }
  }
}

CompletionQueue::CompletionQueueTLSCache::CompletionQueueTLSCache(
    CompletionQueue* cq)
    : cq_(cq), flushed_(false) {
  grpc_completion_queue_thread_local_cache_init(cq_->cq_);
}

CompletionQueue::CompletionQueueTLSCache::~CompletionQueueTLSCache() {
  GPR_ASSERT(flushed_);
}

bool CompletionQueue::CompletionQueueTLSCache::Flush(void** tag, bool* ok) {
  int res = 0;
  void* res_tag;
  flushed_ = true;
  if (grpc_completion_queue_thread_local_cache_flush(cq_->cq_, &res_tag,
                                                     &res)) {
    auto core_cq_tag =
        static_cast<grpc::internal::CompletionQueueTag*>(res_tag);
    *ok = res == 1;
    if (core_cq_tag->FinalizeResult(tag, ok)) {
      return true;
    }
  }
  return false;
}

CompletionQueue* CompletionQueue::CallbackAlternativeCQ() {
  gpr_once_init(&g_once_init_callback_alternative,
                [] { g_callback_alternative_mu = new grpc_core::Mutex(); });
  return g_callback_alternative_cq.Ref();
}

void CompletionQueue::ReleaseCallbackAlternativeCQ(CompletionQueue* cq)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  (void)cq;
  // This accesses g_callback_alternative_cq without acquiring the mutex
  // but it's considered safe because it just reads the pointer address.
  GPR_DEBUG_ASSERT(cq == g_callback_alternative_cq.cq);
  g_callback_alternative_cq.Unref();
}

}  // namespace grpc
