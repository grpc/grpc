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

#ifndef GRPCXX_SUPPORT_CHANNEL_ARGUMENTS_H
#define GRPCXX_SUPPORT_CHANNEL_ARGUMENTS_H

#include <list>
#include <vector>

#include <grpc++/support/config.h>
#include <grpc/compression.h>
#include <grpc/grpc.h>

namespace grpc {
namespace testing {
class ChannelArgumentsTest;
}  // namespace testing

class ResourceQuota;

/// Options for channel creation. The user can use generic setters to pass
/// key value pairs down to c channel creation code. For grpc related options,
/// concrete setters are provided.
class ChannelArguments {
 public:
  ChannelArguments();
  ~ChannelArguments();

  ChannelArguments(const ChannelArguments& other);
  ChannelArguments& operator=(ChannelArguments other) {
    Swap(other);
    return *this;
  }

  void Swap(ChannelArguments& other);

  /// Dump arguments in this instance to \a channel_args. Does not take
  /// ownership of \a channel_args.
  ///
  /// Note that the underlying arguments are shared. Changes made to either \a
  /// channel_args or this instance would be reflected on both.
  void SetChannelArgs(grpc_channel_args* channel_args) const;

  // gRPC specific channel argument setters
  /// Set target name override for SSL host name checking. This option is for
  /// testing only and should never be used in production.
  void SetSslTargetNameOverride(const grpc::string& name);
  // TODO(yangg) add flow control options
  /// Set the compression algorithm for the channel.
  void SetCompressionAlgorithm(grpc_compression_algorithm algorithm);

  /// Set the socket mutator for the channel.
  void SetSocketMutator(grpc_socket_mutator* mutator);

  /// The given string will be sent at the front of the user agent string.
  void SetUserAgentPrefix(const grpc::string& user_agent_prefix);

  /// The given buffer pool will be attached to the constructed channel
  void SetResourceQuota(const ResourceQuota& resource_quota);

  /// Sets the max receive and send message sizes.
  void SetMaxReceiveMessageSize(int size);
  void SetMaxSendMessageSize(int size);

  /// Set LB policy name.
  /// Note that if the name resolver returns only balancer addresses, the
  /// grpclb LB policy will be used, regardless of what is specified here.
  void SetLoadBalancingPolicyName(const grpc::string& lb_policy_name);

  /// Set service config in JSON form.
  /// Primarily meant for use in unit tests.
  void SetServiceConfigJSON(const grpc::string& service_config_json);

  // Generic channel argument setters. Only for advanced use cases.
  /// Set an integer argument \a value under \a key.
  void SetInt(const grpc::string& key, int value);

  // Generic channel argument setter. Only for advanced use cases.
  /// Set a pointer argument \a value under \a key. Owership is not transferred.
  void SetPointer(const grpc::string& key, void* value);

  void SetPointerWithVtable(const grpc::string& key, void* value,
                            const grpc_arg_pointer_vtable* vtable);

  /// Set a textual argument \a value under \a key.
  void SetString(const grpc::string& key, const grpc::string& value);

  /// Return (by value) a c grpc_channel_args structure which points to
  /// arguments owned by this ChannelArguments instance
  grpc_channel_args c_channel_args() const {
    grpc_channel_args out;
    out.num_args = args_.size();
    out.args = args_.empty() ? NULL : const_cast<grpc_arg*>(&args_[0]);
    return out;
  }

 private:
  friend class SecureChannelCredentials;
  friend class testing::ChannelArgumentsTest;

  /// Default pointer argument operations.
  struct PointerVtableMembers {
    static void* Copy(void* in) { return in; }
    static void Destroy(grpc_exec_ctx* exec_ctx, void* in) {}
    static int Compare(void* a, void* b) {
      if (a < b) return -1;
      if (a > b) return 1;
      return 0;
    }
  };

  // Returns empty string when it is not set.
  grpc::string GetSslTargetNameOverride() const;

  std::vector<grpc_arg> args_;
  std::list<grpc::string> strings_;
};

}  // namespace grpc

#endif  // GRPCXX_SUPPORT_CHANNEL_ARGUMENTS_H
