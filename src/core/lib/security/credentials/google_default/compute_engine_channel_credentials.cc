/*
 *
 * Copyright 2020 The gRPC authors.
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

#include "src/core/lib/security/credentials/credentials.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/env.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/http/httpcli.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/security/credentials/alts/alts_credentials.h"
#include "src/core/lib/security/credentials/alts/check_gcp_environment.h"
#include "src/core/lib/security/credentials/google_default/google_default_credentials.h"
#include "src/core/lib/security/credentials/jwt/jwt_credentials.h"
#include "src/core/lib/security/credentials/oauth2/oauth2_credentials.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"
#include "src/core/lib/surface/api_trace.h"

grpc_channel_credentials* grpc_compute_engine_channel_credentials_create(
    void* reserved) {
  grpc_core::ExecCtx exec_ctx;

  GRPC_API_TRACE("grpc_gce_channel_credentials_create(%p)", 1, (reserved));

  GPR_ASSERT(grpc_core::internal::running_on_gce());

  grpc_channel_credentials* ssl_creds =
      grpc_ssl_credentials_create(nullptr, nullptr, nullptr, nullptr);
  GPR_ASSERT(ssl_creds != nullptr);
  grpc_alts_credentials_options* options =
      grpc_alts_credentials_client_options_create();
  grpc_channel_credentials* alts_creds = grpc_alts_credentials_create(options);
  grpc_alts_credentials_options_destroy(options);

  auto creds = new grpc_google_default_channel_credentials(
      alts_creds != nullptr ? alts_creds->Ref() : nullptr,
      ssl_creds != nullptr ? ssl_creds->Ref() : nullptr);
  if (ssl_creds) ssl_creds->Unref();
  if (alts_creds) alts_creds->Unref();

  return creds;
}
