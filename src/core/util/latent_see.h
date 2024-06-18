// Copyright 2024 gRPC authors.
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

#ifndef LATENT_SEE_H
#define LATENT_SEE_H

#include <vector>

#include "time_precise.h"

#define GRPC_ENABLE_LATENT_SEE

#ifdef GRPC_ENABLE_LATENT_SEE
namespace grpc_core {
namespace latent_see {
class Log {
 public:
 private:
  std::vector<Event>* events_;
};

class Scope {
 public:
  explicit Scope(const char* name) : name_(name) {}
  ~Scope() {}

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  const char* const name_;
  gpr_cycle_counter start_{gpr_get_cycle_counter()};
};
}  // namespace latent_see
}  // namespace grpc_core
#define GRPC_LATENT_SEE_SCOPE(name) grpc_core::latent_see::Scope name(#name)
#define GRPC_LATENT_SEE_MARK(name) grpc_core::latent_see::Mark(#name)
#define GRPC_LATENT_SEE_FLOW(name) grpc_core::latent_see::Flow(#name)
#else
namespace grpc_core {
namespace latent_see {
struct Flow {};
}  // namespace latent_see
}  // namespace grpc_core
#define GRPC_LATENT_SEE_SCOPE(name) \
  do {                              \
  } while (0)
#define GRPC_LATENT_SEE_MARK(name) \
  do {                             \
  } while (0)
#define GRPC_LATENT_SEE_FLOW(name) \
  grpc_core::latent_see::Flow {}
#endif

#endif
