// Copyright 2022 gRPC authors.
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

#ifndef EXPERIMENTS_PARSER_H
#define EXPERIMENTS_PARSER_H

#include <grpc/support/port_platform.h>

#include "absl/strings/string_view.h"

namespace grpc_core {

// Return true if experiment \a experiment_id is enabled.
// Experiments are numbered by their order in the g_experiment_metadata array
// declared in experiments.h.
bool IsExperimentEnabled(size_t experiment_id);

}  // namespace grpc_core

#endif
