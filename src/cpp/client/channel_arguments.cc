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

#include <grpc++/channel_arguments.h>

#include <grpc/support/log.h>

#include "src/core/channel/channel_args.h"

namespace grpc {

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
        ap.value.pointer.p = a->value.pointer.copy(ap.value.pointer.p);
        break;
    }
    args_.push_back(ap);
  }
}

void ChannelArguments::Swap(ChannelArguments& other) {
  args_.swap(other.args_);
  strings_.swap(other.strings_);
}

void ChannelArguments::SetCompressionLevel(grpc_compression_level level) {
  SetInt(GRPC_COMPRESSION_LEVEL_ARG, level);
}

void ChannelArguments::SetInt(const grpc::string& key, int value) {
  grpc_arg arg;
  arg.type = GRPC_ARG_INTEGER;
  strings_.push_back(key);
  arg.key = const_cast<char*>(strings_.back().c_str());
  arg.value.integer = value;

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
