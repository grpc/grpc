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

#include "src/core/lib/channel/channelz.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/lib/channel/channelz_registry.h"
#include "src/core/lib/channel/status_util.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/connectivity_state.h"
#include "src/core/lib/transport/error_utils.h"

// TODO(ncteisen): actually implement this
char* grpc_channelz_get_channel(intptr_t channel_id) { return nullptr; }

namespace grpc_core {
namespace channelz {

// TODO(ncteisen): more this functions to a loc where it can be used
namespace {

// returns an allocated string that represents tm according to RFC-3339, and,
// more specifically, follows:
// https://developers.google.com/protocol-buffers/docs/proto3#json
//
// "Uses RFC 3339, where generated output will always be Z-normalized and uses
// 0, 3, 6 or 9 fractional digits."
char* fmt_time(gpr_timespec tm) {
  char time_buffer[35];
  char ns_buffer[11];  // '.' + 9 digits of precision
  struct tm* tm_info = localtime((const time_t*)&tm.tv_sec);
  strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
  snprintf(ns_buffer, 11, ".%09d", tm.tv_nsec);
  // This loop trims off trailing zeros by inserting a null character that the
  // right point. We iterate in chunks of three because we want 0, 3, 6, or 9
  // fractional digits.
  for (int i = 7; i >= 1; i -= 3) {
    if (ns_buffer[i] == '0' && ns_buffer[i + 1] == '0' &&
        ns_buffer[i + 2] == '0') {
      ns_buffer[i] = '\0';
      // Edge case in which all fractional digits were 0.
      if (i == 1) {
        ns_buffer[0] = '\0';
      }
    } else {
      break;
    }
  }
  char* full_time_str;
  gpr_asprintf(&full_time_str, "%s%sZ", time_buffer, ns_buffer);
  return full_time_str;
}

// TODO(ncteisen); move this to json library
grpc_json* add_num_str(grpc_json* parent, grpc_json* it, const char* name,
                       uint64_t num) {
  char* num_str;
  gpr_asprintf(&num_str, "%" PRIu64, num);
  return grpc_json_create_child(it, parent, name, num_str, GRPC_JSON_STRING,
                                true);
}

}  // namespace

Channel::Channel(grpc_channel* channel, size_t channel_tracer_max_nodes)
    : channel_(channel) {
  trace_.Init(channel_tracer_max_nodes);
  target_ = grpc_channel_get_target(channel_);
  channel_uuid_ = ChannelzRegistry::Register(this);
}

Channel::~Channel() {
  gpr_free(const_cast<char*>(target_));
  ChannelzRegistry::Unregister(channel_uuid_);
}

void Channel::CallStarted() {
  calls_started_++;
  last_call_started_timestamp_ =
      grpc_millis_to_timespec(ExecCtx::Get()->Now(), GPR_CLOCK_REALTIME);
}

grpc_connectivity_state Channel::GetConnectivityState() {
  if (channel_destroyed_) {
    return GRPC_CHANNEL_SHUTDOWN;
  } else {
    return grpc_channel_check_connectivity_state(channel_, false);
  }
}

char* Channel::RenderJSON() {
  // We need to track these three json objects to build our object
  grpc_json* top_level_json = grpc_json_create(GRPC_JSON_OBJECT);
  grpc_json* json = top_level_json;
  grpc_json* json_iterator = nullptr;

  // create and fill the ref child
  json_iterator = grpc_json_create_child(json_iterator, json, "ref", nullptr,
                                         GRPC_JSON_OBJECT, true);
  json = json_iterator;
  json_iterator = nullptr;
  json_iterator = add_num_str(json, json_iterator, "channelId", channel_uuid_);

  // reset json iterators to top level object
  json = top_level_json;
  json_iterator = nullptr;

  // create and fill the data child
  json_iterator = grpc_json_create_child(json_iterator, json, "data", nullptr,
                                         GRPC_JSON_OBJECT, true);
  json = json_iterator;
  json_iterator = nullptr;
  json_iterator =
      add_num_str(json, json_iterator, "callsStarted", calls_started_);
  json_iterator =
      add_num_str(json, json_iterator, "callsSucceeded", calls_succeeded_);
  json_iterator =
      add_num_str(json, json_iterator, "callsFailed", calls_failed_);
  json_iterator = grpc_json_create_child(
      json_iterator, json, "lastCallStartedTimestamp",
      fmt_time(last_call_started_timestamp_), GRPC_JSON_STRING, true);
  json_iterator = grpc_json_create_child(json_iterator, json, "target", target_,
                                         GRPC_JSON_STRING, false);
  grpc_connectivity_state connectivity_state = GetConnectivityState();
  json_iterator =
      grpc_json_create_child(json_iterator, json, "state",
                             grpc_connectivity_state_name(connectivity_state),
                             GRPC_JSON_STRING, false);
  char* trace = trace_->RenderTrace();
  if (trace != nullptr) {
    json_iterator = grpc_json_create_child(json_iterator, json, "trace", trace,
                                           GRPC_JSON_STRING, true);
  }

  // render and return the over json object
  char* json_str = grpc_json_dump_to_string(top_level_json, 0);
  grpc_json_destroy(top_level_json);
  return json_str;
}

}  // namespace channelz
}  // namespace grpc_core
