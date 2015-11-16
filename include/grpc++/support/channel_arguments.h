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

#include <vector>
#include <list>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc++/support/config.h>

namespace grpc {
namespace testing {
class ChannelArgumentsTest;
}  // namespace testing

/// Options for channel creation. The user can use generic setters to pass
/// key value pairs down to c channel creation code. For grpc related options,
/// concrete setters are provided.
class ChannelArguments {
 public:
  ChannelArguments() {}
  ~ChannelArguments() {}

  ChannelArguments(const ChannelArguments& other);
  ChannelArguments& operator=(ChannelArguments other) {
    Swap(other);
    return *this;
  }

  void Swap(ChannelArguments& other);

  /// Populates this instance with the arguments from \a channel_args. Does not
  /// take ownership of \a channel_args.
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

  // Generic channel argument setters. Only for advanced use cases.
  /// Set an integer argument \a value under \a key.
  void SetInt(const grpc::string& key, int value);
  /// Set a textual argument \a value under \a key.
  void SetString(const grpc::string& key, const grpc::string& value);

 private:
  friend class SecureCredentials;
  friend class testing::ChannelArgumentsTest;

  // Returns empty string when it is not set.
  grpc::string GetSslTargetNameOverride() const;

  std::vector<grpc_arg> args_;
  std::list<grpc::string> strings_;
};

}  // namespace grpc

#endif  // GRPCXX_SUPPORT_CHANNEL_ARGUMENTS_H
