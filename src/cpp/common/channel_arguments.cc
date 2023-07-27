//
//
// Copyright 2015 gRPC authors.
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
//
//
#include <algorithm>
#include <list>
#include <string>
#include <vector>

#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/compression_types.h>
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/resource_quota.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/socket_mutator.h"

namespace grpc {

ChannelArguments::ChannelArguments() {
  // This will be ignored if used on the server side.
  SetString(GRPC_ARG_PRIMARY_USER_AGENT_STRING, "grpc-c++/" + grpc::Version());
}

ChannelArguments::ChannelArguments(const ChannelArguments& other)
    : strings_(other.strings_) {
  args_.reserve(other.args_.size());
  auto list_it_dst = strings_.begin();
  auto list_it_src = other.strings_.begin();
  for (const auto& a : other.args_) {
    grpc_arg ap;
    ap.type = a.type;
    GPR_ASSERT(list_it_src->c_str() == a.key);
    ap.key = const_cast<char*>(list_it_dst->c_str());
    ++list_it_src;
    ++list_it_dst;
    switch (a.type) {
      case GRPC_ARG_INTEGER:
        ap.value.integer = a.value.integer;
        break;
      case GRPC_ARG_STRING:
        GPR_ASSERT(list_it_src->c_str() == a.value.string);
        ap.value.string = const_cast<char*>(list_it_dst->c_str());
        ++list_it_src;
        ++list_it_dst;
        break;
      case GRPC_ARG_POINTER:
        ap.value.pointer = a.value.pointer;
        ap.value.pointer.p = a.value.pointer.vtable->copy(ap.value.pointer.p);
        break;
    }
    args_.push_back(ap);
  }
}

ChannelArguments::~ChannelArguments() {
  for (auto& arg : args_) {
    if (arg.type == GRPC_ARG_POINTER) {
      grpc_core::ExecCtx exec_ctx;
      arg.value.pointer.vtable->destroy(arg.value.pointer.p);
    }
  }
}

void ChannelArguments::Swap(ChannelArguments& other) {
  args_.swap(other.args_);
  strings_.swap(other.strings_);
}

void ChannelArguments::SetCompressionAlgorithm(
    grpc_compression_algorithm algorithm) {
  SetInt(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM, algorithm);
}

void ChannelArguments::SetGrpclbFallbackTimeout(int fallback_timeout) {
  SetInt(GRPC_ARG_GRPCLB_FALLBACK_TIMEOUT_MS, fallback_timeout);
}

void ChannelArguments::SetSocketMutator(grpc_socket_mutator* mutator) {
  if (!mutator) {
    return;
  }
  grpc_arg mutator_arg = grpc_socket_mutator_to_arg(mutator);
  bool replaced = false;
  grpc_core::ExecCtx exec_ctx;
  for (auto& arg : args_) {
    if (arg.type == mutator_arg.type &&
        std::string(arg.key) == std::string(mutator_arg.key)) {
      GPR_ASSERT(!replaced);
      arg.value.pointer.vtable->destroy(arg.value.pointer.p);
      arg.value.pointer = mutator_arg.value.pointer;
      replaced = true;
    }
  }

  if (!replaced) {
    strings_.push_back(std::string(mutator_arg.key));
    args_.push_back(mutator_arg);
    args_.back().key = const_cast<char*>(strings_.back().c_str());
  }
}

// Note: a second call to this will add in front the result of the first call.
// An example is calling this on a copy of ChannelArguments which already has a
// prefix. The user can build up a prefix string by calling this multiple times,
// each with more significant identifier.
void ChannelArguments::SetUserAgentPrefix(
    const std::string& user_agent_prefix) {
  if (user_agent_prefix.empty()) {
    return;
  }
  bool replaced = false;
  auto strings_it = strings_.begin();
  for (auto& arg : args_) {
    ++strings_it;
    if (arg.type == GRPC_ARG_STRING) {
      if (std::string(arg.key) == GRPC_ARG_PRIMARY_USER_AGENT_STRING) {
        GPR_ASSERT(arg.value.string == strings_it->c_str());
        *(strings_it) = user_agent_prefix + " " + arg.value.string;
        arg.value.string = const_cast<char*>(strings_it->c_str());
        replaced = true;
        break;
      }
      ++strings_it;
    }
  }
  if (!replaced) {
    SetString(GRPC_ARG_PRIMARY_USER_AGENT_STRING, user_agent_prefix);
  }
}

void ChannelArguments::SetResourceQuota(
    const grpc::ResourceQuota& resource_quota) {
  SetPointerWithVtable(GRPC_ARG_RESOURCE_QUOTA,
                       resource_quota.c_resource_quota(),
                       grpc_resource_quota_arg_vtable());
}

void ChannelArguments::SetMaxReceiveMessageSize(int size) {
  SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, size);
}

void ChannelArguments::SetMaxSendMessageSize(int size) {
  SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH, size);
}

void ChannelArguments::SetLoadBalancingPolicyName(
    const std::string& lb_policy_name) {
  SetString(GRPC_ARG_LB_POLICY_NAME, lb_policy_name);
}

void ChannelArguments::SetServiceConfigJSON(
    const std::string& service_config_json) {
  SetString(GRPC_ARG_SERVICE_CONFIG, service_config_json);
}

void ChannelArguments::SetInt(const std::string& key, int value) {
  grpc_arg arg;
  arg.type = GRPC_ARG_INTEGER;
  strings_.push_back(key);
  arg.key = const_cast<char*>(strings_.back().c_str());
  arg.value.integer = value;

  args_.push_back(arg);
}

void ChannelArguments::SetPointer(const std::string& key, void* value) {
  static const grpc_arg_pointer_vtable vtable = {
      &PointerVtableMembers::Copy, &PointerVtableMembers::Destroy,
      &PointerVtableMembers::Compare};
  SetPointerWithVtable(key, value, &vtable);
}

void ChannelArguments::SetPointerWithVtable(
    const std::string& key, void* value,
    const grpc_arg_pointer_vtable* vtable) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  strings_.push_back(key);
  arg.key = const_cast<char*>(strings_.back().c_str());
  arg.value.pointer.p = vtable->copy(value);
  arg.value.pointer.vtable = vtable;
  args_.push_back(arg);
}

void ChannelArguments::SetString(const std::string& key,
                                 const std::string& value) {
  grpc_arg arg;
  arg.type = GRPC_ARG_STRING;
  strings_.push_back(key);
  arg.key = const_cast<char*>(strings_.back().c_str());
  strings_.push_back(value);
  arg.value.string = const_cast<char*>(strings_.back().c_str());

  args_.push_back(arg);
}

void ChannelArguments::SetChannelArgs(grpc_channel_args* channel_args) const {
  channel_args->num_args = args_.size();
  if (channel_args->num_args > 0) {
    channel_args->args = const_cast<grpc_arg*>(&args_[0]);
  }
}

}  // namespace grpc
