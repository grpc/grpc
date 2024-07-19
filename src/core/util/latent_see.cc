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

#include "src/core/util/latent_see.h"

#ifdef GRPC_ENABLE_LATENT_SEE
#include "src/core/util/latent_see_impl.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace latent_see {

namespace {
LogFn g_logger = nullptr;
}  // namespace

MarkerStorage::Ops MarkerStorage::ops_ = {
    .construct =
        [](MarkerStorage* marker, const char* name, MarkerType /*type*/,
           const char* file, int line, const char* /*function*/) {
          static_assert(sizeof(MarkerStorage) >= sizeof(Metadata));
          new (marker->GetStorageAs<Metadata>()) Metadata(file, line, name);
        },
    .destruct = [](MarkerStorage* /*marker*/) {}};

template <>
ParentScopeTimerStorage::Ops ParentScopeTimerStorage::ops_ = {
    .construct =
        [](ParentScopeTimerStorage* timer, MarkerStorage* marker) {
          static_assert(sizeof(ParentScopeTimerStorage) >= sizeof(ParentScope));
          new (timer->GetStorageAs<ParentScope>())
              ParentScope(marker->GetStorageAs<Metadata>());
        },
    .destruct =
        [](ParentScopeTimerStorage* timer) {
          timer->GetStorageAs<ParentScope>()->~ParentScope();
        }};

template <>
InnerScopeTimerStorage::Ops ScopedTimerStorage<false>::ops_ = {
    .construct =
        [](InnerScopeTimerStorage* timer, MarkerStorage* marker) {
          static_assert(sizeof(InnerScopeTimerStorage) >= sizeof(InnerScope));
          new (timer->GetStorageAs<InnerScope>())
              InnerScope(marker->GetStorageAs<Metadata>());
        },
    .destruct =
        [](InnerScopeTimerStorage* timer) {
          timer->GetStorageAs<InnerScope>()->~InnerScope();
        }};

GroupThreadScopeStorage::Ops GroupThreadScopeStorage::ops_ = {
    .construct =
        [](GroupThreadScopeStorage* timer, MarkerStorage* marker,
           absl::string_view name) {
          static_assert(sizeof(GroupThreadScopeStorage) >= sizeof(Flow));
          new (timer->GetStorageAs<Flow>())
              Flow(marker->GetStorageAs<Metadata>());
        },
    .destruct =
        [](GroupThreadScopeStorage* timer) {
          timer->GetStorageAs<Flow>()->~Flow();
        }};

void MarkerStorage::OverrideOps(MarkerStorage::Ops ops) { ops_ = ops; }

void GroupThreadScopeStorage::OverrideOps(GroupThreadScopeStorage::Ops ops) {
  ops_ = ops;
}

template <>
void ParentScopeTimerStorage::OverrideOps(ParentScopeTimerStorage::Ops ops) {
  ops_ = ops;
}

template <>
void InnerScopeTimerStorage::OverrideOps(InnerScopeTimerStorage::Ops ops) {
  ops_ = ops;
}

void SetEventLogger(LogFn logger) { g_logger = logger; }

void LogEvent(MarkerStorage* marker) {
  if (g_logger != nullptr) {
    return g_logger(marker);
  }
  Mark(marker->GetStorageAs<Metadata>());
}

}  // namespace latent_see
}  // namespace grpc_core
#endif
