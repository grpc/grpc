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

#include "test/core/security/oauth2_utils.h"

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/exec_ctx_wakeup_scheduler.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/security/context/security_context.h"
#include "src/core/lib/security/credentials/credentials.h"

char* grpc_test_fetch_oauth2_token_with_credentials(
    grpc_call_credentials* creds) {
  grpc_core::ExecCtx exec_ctx;
  grpc_call_credentials::GetRequestMetadataArgs get_request_metadata_args;
  grpc_core::Notification done;
  grpc_core::MemoryAllocator memory_allocator =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  auto arena = grpc_core::MakeScopedArena(1024, &memory_allocator);
  grpc_metadata_batch initial_metadata{arena.get()};
  char* token = nullptr;
  // TODO(hork): rm once GetRequestMetadata does not depend on pollsets.
  grpc_pollset* pollset =
      static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  gpr_mu* mu = nullptr;
  grpc_pollset_init(pollset, &mu);
  auto pops = grpc_polling_entity_create_from_pollset(pollset);
  auto activity = grpc_core::MakeActivity(
      [creds, &initial_metadata, &get_request_metadata_args]() {
        return grpc_core::Map(
            creds->GetRequestMetadata(
                grpc_core::ClientMetadataHandle(
                    &initial_metadata,
                    grpc_core::Arena::PooledDeleter(nullptr)),
                &get_request_metadata_args),
            [](const absl::StatusOr<grpc_core::ClientMetadataHandle>& s) {
              return s.status();
            });
      },
      grpc_core::ExecCtxWakeupScheduler(),
      [&done, &token, &initial_metadata](absl::Status result) {
        if (!result.ok()) {
          gpr_log(GPR_ERROR, "Fetching token failed: %s",
                  result.ToString().c_str());
        } else {
          std::string buffer;
          token = gpr_strdup(
              std::string(
                  initial_metadata
                      .GetStringValue(GRPC_AUTHORIZATION_METADATA_KEY, &buffer)
                      .value_or(""))
                  .c_str());
        }
        done.Notify();
      },
      arena.get(), &pops);
  grpc_core::ExecCtx::Get()->Flush();
  done.WaitForNotification();
  grpc_core::ExecCtx::Get()->Flush();
  return token;
}
