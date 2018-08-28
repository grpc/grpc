/*
 *
 * Copyright 2017 gRPC authors.
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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/channel/channelz.h"
#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/gprpp/mutex_lock.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <cstring>

namespace grpc_core {
namespace channelz {
namespace {

// singleton instance of the registry.
ChannelzRegistry* g_channelz_registry = nullptr;

}  // anonymous namespace

void ChannelzRegistry::Init() { g_channelz_registry = New<ChannelzRegistry>(); }

void ChannelzRegistry::Shutdown() { Delete(g_channelz_registry); }

ChannelzRegistry* ChannelzRegistry::Default() {
  GPR_DEBUG_ASSERT(g_channelz_registry != nullptr);
  return g_channelz_registry;
}

ChannelzRegistry::ChannelzRegistry() { gpr_mu_init(&mu_); }

ChannelzRegistry::~ChannelzRegistry() { gpr_mu_destroy(&mu_); }

intptr_t ChannelzRegistry::InternalRegisterEntry(const RegistryEntry& entry) {
  MutexLock lock(&mu_);
  entities_.push_back(entry);
  intptr_t uuid = entities_.size();
  return uuid;
}

void ChannelzRegistry::InternalUnregisterEntry(intptr_t uuid, EntityType type) {
  GPR_ASSERT(uuid >= 1);
  MutexLock lock(&mu_);
  GPR_ASSERT(static_cast<size_t>(uuid) <= entities_.size());
  GPR_ASSERT(entities_[uuid - 1].type == type);
  entities_[uuid - 1].object = nullptr;
  entities_[uuid - 1].type = EntityType::kUnset;
}

void* ChannelzRegistry::InternalGetEntry(intptr_t uuid, EntityType type) {
  MutexLock lock(&mu_);
  if (uuid < 1 || uuid > static_cast<intptr_t>(entities_.size())) {
    return nullptr;
  }
  if (entities_[uuid - 1].type == type) {
    return entities_[uuid - 1].object;
  } else {
    return nullptr;
  }
}

char* ChannelzRegistry::InternalGetTopChannels(intptr_t start_channel_id) {
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;
  InlinedVector<ChannelNode*, 10> top_level_channels;
  // uuids index into entities one-off (idx 0 is really uuid 1, since 0 is
  // reserved). However, we want to support requests coming in with
  // start_channel_id=0, which signifies "give me everything." Hence this
  // funky looking line below.
  size_t start_idx = start_channel_id == 0 ? 0 : start_channel_id - 1;
  for (size_t i = start_idx; i < entities_.size(); ++i) {
    if (entities_[i].type == EntityType::kChannelNode) {
      ChannelNode* channel_node =
          static_cast<ChannelNode*>(entities_[i].object);
      if (channel_node->is_top_level_channel()) {
        top_level_channels.push_back(channel_node);
      }
    }
  }
  if (top_level_channels.size() > 0) {
    // create list of channels
    grpc_json* array_parent = grpc_json_create_child(
        nullptr, json, "channel", nullptr, GRPC_JSON_ARRAY, false);
    for (size_t i = 0; i < top_level_channels.size(); ++i) {
      grpc_json* channel_json = top_level_channels[i]->RenderJson();
      json_iterator =
          grpc_json_link_child(array_parent, channel_json, json_iterator);
    }
  }
  // For now we do not have any pagination rules. In the future we could
  // pick a constant for max_channels_sent for a GetTopChannels request.
  // Tracking: https://github.com/grpc/grpc/issues/16019.
  json_iterator = grpc_json_create_child(nullptr, json, "end", nullptr,
                                         GRPC_JSON_TRUE, false);
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

}  // namespace channelz
}  // namespace grpc_core

char* grpc_channelz_get_top_channels(intptr_t start_channel_id) {
  return grpc_core::channelz::ChannelzRegistry::GetTopChannels(
      start_channel_id);
}

char* grpc_channelz_get_channel(intptr_t channel_id) {
  grpc_core::channelz::ChannelNode* channel_node =
      grpc_core::channelz::ChannelzRegistry::GetChannelNode(channel_id);
  if (channel_node == nullptr) {
    return nullptr;
  }
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* channel_json = channel_node->RenderJson();
  channel_json->key = "channel";
  grpc_json_link_child(json, channel_json, nullptr);
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}
