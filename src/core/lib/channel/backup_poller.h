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

#ifndef GRPC_CORE_LIB_CHANNEL_BACKUP_POLLER_H
#define GRPC_CORE_LIB_CHANNEL_BACKUP_POLLER_H

#include <grpc/support/port_platform.h>

#include <thread>

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {
class BackupPoller {
 public:
  static BackupPoller* Get();

  void StartPolling(grpc_pollset_set* interested_parties);
  void StopPolling(grpc_pollset_set* interested_parties);

  BackupPoller(const BackupPoller&) = delete;
  BackupPoller& operator=(const BackupPoller&) = delete;

 private:
  BackupPoller();
  ~BackupPoller();
  class Poller;

  void Run();

  Mutex mu_;
  int interested_parties_ ABSL_GUARDED_BY(mu_) = 0;
  Poller* poller_ ABSL_GUARDED_BY(mu_) = nullptr;
};
}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_CHANNEL_BACKUP_POLLER_H */
