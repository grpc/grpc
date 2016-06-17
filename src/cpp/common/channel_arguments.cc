/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <grpc++/support/channel_arguments.h>

#include <sstream>

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>
#include "src/core/lib/channel/channel_args.h"

namespace grpc {

ChannelArguments::ChannelArguments() {
  std::ostringstream user_agent_prefix;
  user_agent_prefix << "grpc-c++/" << grpc_version_string();
  // This will be ignored if used on the server side.
  SetString(GRPC_ARG_PRIMARY_USER_AGENT_STRING, user_agent_prefix.str());
}

ChannelArguments::ChannelArguments(const ChannelArguments& other)
    : strings_(other.strings_) {
  args_.reserve(other.args_.size());
  auto list_it_dst = strings_.begin();
  auto list_it_src = other.strings_.begin();
  for (auto a = other.args_.begin(); a != other.args_.end(); ++a) {
    grpc_arg ap;
    ap.type = a->type;
    GPR_ASSERT(list_it_src->c_str() == a->key);
    ap.key = const_cast<char*>(list_it_dst->c_str());
    ++list_it_src;
    ++list_it_dst;
    switch (a->type) {
      case GRPC_ARG_INTEGER:
        ap.value.integer = a->value.integer;
        break;
      case GRPC_ARG_STRING:
        GPR_ASSERT(list_it_src->c_str() == a->value.string);
        ap.value.string = const_cast<char*>(list_it_dst->c_str());
        ++list_it_src;
        ++list_it_dst;
        break;
      case GRPC_ARG_POINTER:
        ap.value.pointer = a->value.pointer;
        ap.value.pointer.p = a->value.pointer.vtable->copy(ap.value.pointer.p);
        break;
    }
    args_.push_back(ap);
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

// Note: a second call to this will add in front the result of the first call.
// An example is calling this on a copy of ChannelArguments which already has a
// prefix. The user can build up a prefix string by calling this multiple times,
// each with more significant identifier.
void ChannelArguments::SetUserAgentPrefix(
    const grpc::string& user_agent_prefix) {
  if (user_agent_prefix.empty()) {
    return;
  }
  bool replaced = false;
  for (auto it = args_.begin(); it != args_.end(); ++it) {
    const grpc_arg& arg = *it;
    if (arg.type == GRPC_ARG_STRING &&
        grpc::string(arg.key) == GRPC_ARG_PRIMARY_USER_AGENT_STRING) {
      strings_.push_back(user_agent_prefix + " " + arg.value.string);
      it->value.string = const_cast<char*>(strings_.back().c_str());
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    SetString(GRPC_ARG_PRIMARY_USER_AGENT_STRING, user_agent_prefix);
  }
}

void ChannelArguments::SetInt(const grpc::string& key, int value) {
  grpc_arg arg;
  arg.type = GRPC_ARG_INTEGER;
  strings_.push_back(key);
  arg.key = const_cast<char*>(strings_.back().c_str());
  arg.value.integer = value;

  args_.push_back(arg);
}

void ChannelArguments::SetPointer(const grpc::string& key, void* value) {
  static const grpc_arg_pointer_vtable vtable = {
      &PointerVtableMembers::Copy, &PointerVtableMembers::Destroy,
      &PointerVtableMembers::Compare};
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  strings_.push_back(key);
  arg.key = const_cast<char*>(strings_.back().c_str());
  arg.value.pointer.p = value;
  arg.value.pointer.vtable = &vtable;
  args_.push_back(arg);
}

void ChannelArguments::SetString(const grpc::string& key,
                                 const grpc::string& value) {
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
