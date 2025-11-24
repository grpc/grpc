// Copyright 2025 gRPC authors.
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

#include "src/core/util/postmortem_emit.h"

#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "src/core/channelz/channelz_registry.h"
#include "src/core/channelz/zviz/entity.h"
#include "src/core/channelz/zviz/environment.h"
#include "src/core/channelz/zviz/layout.h"
#include "src/core/channelz/zviz/layout_text.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/telemetry/stats.h"
#include "src/proto/grpc/channelz/v2/channelz.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

namespace grpc_core {

namespace {

class PostMortemEnvironment : public grpc_zviz::Environment {
 public:
  explicit PostMortemEnvironment(
      const std::map<int64_t, grpc::channelz::v2::Entity>& entities)
      : entities_(entities) {}

  std::string EntityLinkTarget(int64_t entity_id) override {
    return absl::StrCat("#", entity_id);
  }

  absl::StatusOr<grpc::channelz::v2::Entity> GetEntity(
      int64_t entity_id) override {
    auto it = entities_.find(entity_id);
    if (it == entities_.end()) {
      return absl::NotFoundError(absl::StrCat("Entity not found: ", entity_id));
    }
    return it->second;
  }

 private:
  std::map<int64_t, grpc::channelz::v2::Entity> entities_;
};

void RunPostMortem(std::ostream& out) {
  out << "===========================================================\n";
  out << "🛑 gRPC Test Postmortem Analysis 🛑\n";
  out << "===========================================================\n";

  out << "❗ gRPC Statistics:\n"
      << StatsAsJson(global_stats().Collect().get()) << "\n";

  out << "❗ channelz entities:\n";
  std::map<int64_t, grpc::channelz::v2::Entity> entities;
  std::vector<grpc::channelz::v2::Entity> entities_list;
  for (const auto& node : channelz::ChannelzRegistry::GetAllEntities()) {
    grpc::channelz::v2::Entity entity;
    if (entity.ParseFromString(
            node->SerializeEntityToString(absl::Milliseconds(100)))) {
      entities[node->uuid()] = entity;
      entities_list.push_back(entity);
    }
  }
  PostMortemEnvironment env(entities);
  grpc_zviz::layout::TextElement root;
  for (const auto& entity : entities_list) {
    grpc_zviz::Format(env, entity, root);
  }
  out << root.Render() << "\n";
}

}  // namespace

void PostMortemEmit() { RunPostMortem(std::cerr); }

void SilentPostMortemEmit() {
  ExecCtx exec_ctx;
  std::ostringstream out;
  RunPostMortem(out);
}

}  // namespace grpc_core
