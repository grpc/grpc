/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_GOOGLE_DEFAULT_GOOGLE_DEFAULT_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_GOOGLE_DEFAULT_GOOGLE_DEFAULT_CREDENTIALS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/credentials/credentials.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GRPC_GOOGLE_CLOUD_SDK_CONFIG_DIRECTORY "gcloud"
#define GRPC_GOOGLE_WELL_KNOWN_CREDENTIALS_FILE \
  "application_default_credentials.json"

#ifdef GPR_WINDOWS
#define GRPC_GOOGLE_CREDENTIALS_PATH_ENV_VAR "APPDATA"
#define GRPC_GOOGLE_CREDENTIALS_PATH_SUFFIX \
  GRPC_GOOGLE_CLOUD_SDK_CONFIG_DIRECTORY    \
  "/" GRPC_GOOGLE_WELL_KNOWN_CREDENTIALS_FILE
#else
#define GRPC_GOOGLE_CREDENTIALS_PATH_ENV_VAR "HOME"
#define GRPC_GOOGLE_CREDENTIALS_PATH_SUFFIX         \
  ".config/" GRPC_GOOGLE_CLOUD_SDK_CONFIG_DIRECTORY \
  "/" GRPC_GOOGLE_WELL_KNOWN_CREDENTIALS_FILE
#endif

void grpc_flush_cached_google_default_credentials(void);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_GOOGLE_DEFAULT_GOOGLE_DEFAULT_CREDENTIALS_H \
        */
