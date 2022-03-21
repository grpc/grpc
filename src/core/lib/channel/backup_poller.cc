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

#include "src/core/lib/channel/backup_poller.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/global_config.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/pollset.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/surface/completion_queue.h"

namespace grpc_core {

class BackupPoller::Poller {
 public:
  Poller();
  ~Poller();

  void Add(grpc_pollset_set* interested_parties) {
    grpc_pollset_set_add_pollset(interested_parties, pollset_);
  }

  void Remove(grpc_pollset_set* interested_parties) {
    grpc_pollset_set_del_pollset(interested_parties, pollset_);
  }

 private:
  void Run();

  gpr_mu* mu_{nullptr};
  grpc_pollset* pollset_;
  std::atomic<bool> shutdown_{false};
  std::thread thread_;
};

BackupPoller::BackupPoller() = default;

BackupPoller* BackupPoller::Get() {
  static BackupPoller* p = new BackupPoller();
  return p;
}

void BackupPoller::StartPolling(grpc_pollset_set* interested_parties) {
  MutexLock lock(&mu_);
  ++interested_parties_;
  if (interested_parties_ == 1) {
    GPR_ASSERT(poller_ == nullptr);
    poller_ = new Poller;
  }
  poller_->Add(interested_parties);
}

void BackupPoller::StopPolling(grpc_pollset_set* interested_parties) {
  MutexLock lock(&mu_);
  poller_->Remove(interested_parties);
  --interested_parties_;
  if (0 == interested_parties_) {
    auto* poller = absl::exchange(poller_, nullptr);
    std::thread([poller]() { delete poller; }).detach();
  }
}

BackupPoller::Poller::Poller() {
  pollset_ = static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  grpc_pollset_init(pollset_, &mu_);
  thread_ = std::thread(&BackupPoller::Poller::Run, this);
}

BackupPoller::Poller::~Poller() {
  shutdown_.store(true);
  struct PollsetShutdown {
    explicit PollsetShutdown(grpc_pollset* pollset) : pollset(pollset) {}
    ~PollsetShutdown() {
      grpc_pollset_destroy(pollset);
      gpr_free(pollset);
    }
    grpc_pollset* const pollset;
    std::atomic<int> refs{2};
    void Unref() {
      if (refs.fetch_sub(1) == 1) delete this;
    }
  };
  PollsetShutdown* p = new PollsetShutdown(pollset_);
  {
    ExecCtx exec_ctx;
    grpc_closure* done = GRPC_CLOSURE_CREATE(
        [](void* arg, grpc_error*) {
          static_cast<PollsetShutdown*>(arg)->Unref();
        },
        p, nullptr);
    gpr_mu_lock(mu_);
    grpc_pollset_shutdown(pollset_, done);
    gpr_mu_unlock(mu_);
  }
  thread_.join();
  p->Unref();
}

void BackupPoller::Poller::Run() {
  while (!shutdown_.load()) {
    ExecCtx exec_ctx;

    gpr_mu_lock(mu_);
    grpc_error_handle err =
        grpc_pollset_work(pollset_, nullptr, Timestamp::InfFuture());
    gpr_mu_unlock(mu_);

    if (err != GRPC_ERROR_NONE) {
      gpr_log(GPR_DEBUG, "backup poller gets error: %s",
              grpc_error_std_string(err).c_str());
      break;
    }
  }
}

}  // namespace grpc_core
