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

  grpc_pollset* pollset =
      static_cast<grpc_pollset*>(gpr_zalloc(grpc_pollset_size()));
  gpr_mu* mu = nullptr;
  grpc_pollset_init(pollset, &mu);
  auto pops = grpc_polling_entity_create_from_pollset(pollset);
  bool is_done = false;

  grpc_core::MemoryAllocator memory_allocator =
      grpc_core::MemoryAllocator(grpc_core::ResourceQuota::Default()
                                     ->memory_quota()
                                     ->CreateMemoryAllocator("test"));
  auto arena = grpc_core::MakeScopedArena(1024, &memory_allocator);
  grpc_metadata_batch initial_metadata{arena.get()};
  char* token = nullptr;

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
      [&is_done, &token, &initial_metadata](absl::Status result) {
        is_done = true;
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
      },
      arena.get(), &pops);

  grpc_core::ExecCtx::Get()->Flush();

  gpr_mu_lock(mu);
  while (!is_done) {
    grpc_pollset_worker* worker = nullptr;
    if (!GRPC_LOG_IF_ERROR(
            "pollset_work",
            grpc_pollset_work(grpc_polling_entity_pollset(&pops), &worker,
                              grpc_core::Timestamp::InfFuture()))) {
      is_done = true;
    }
  }
  gpr_mu_unlock(mu);

  grpc_pollset_shutdown(
      grpc_polling_entity_pollset(&pops),
      GRPC_CLOSURE_CREATE([](void*, grpc_error_handle) {}, nullptr, nullptr));
  grpc_core::ExecCtx::Get()->Flush();
  grpc_pollset_destroy(grpc_polling_entity_pollset(&pops));
  gpr_free(pollset);

  return token;
}
