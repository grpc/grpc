/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/census/census_interface.h"

#include <grpc/support/log.h>
#include "src/core/ext/census/census_rpc_stats.h"
#include "src/core/ext/census/census_tracing.h"

void census_init(void) {
  census_tracing_init();
  census_stats_store_init();
}

void census_shutdown(void) {
  census_stats_store_shutdown();
  census_tracing_shutdown();
}
