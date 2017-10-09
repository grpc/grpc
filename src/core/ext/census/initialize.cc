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

#include <grpc/census.h>
#include "src/core/ext/census/base_resources.h"
#include "src/core/ext/census/resource.h"

static int features_enabled = CENSUS_FEATURE_NONE;

int census_initialize(int features) {
  if (features_enabled != CENSUS_FEATURE_NONE) {
    // Must have been a previous call to census_initialize; return error
    return -1;
  }
  features_enabled = features & CENSUS_FEATURE_ALL;
  if (features & CENSUS_FEATURE_STATS) {
    initialize_resources();
    define_base_resources();
  }

  return features_enabled;
}

void census_shutdown(void) {
  if (features_enabled & CENSUS_FEATURE_STATS) {
    shutdown_resources();
  }
  features_enabled = CENSUS_FEATURE_NONE;
}

int census_supported(void) {
  /* TODO(aveitch): improve this as we implement features... */
  return CENSUS_FEATURE_NONE;
}

int census_enabled(void) { return features_enabled; }
