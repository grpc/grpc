/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_KEY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_KEY_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/log.h>

#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"

namespace grpc_core {

// A key that can uniquely identify a subchannel.
class SubchannelKey {
 public:
  explicit SubchannelKey(const grpc_subchannel_args* args);
  ~SubchannelKey();

  // Copyable.
  SubchannelKey(const SubchannelKey& other);
  SubchannelKey& operator=(const SubchannelKey& other);
  // Not movable.
  SubchannelKey(SubchannelKey&&) = delete;
  SubchannelKey& operator=(SubchannelKey&&) = delete;

  int Cmp(const SubchannelKey& other) const;

  // Sets whether subchannel keys are always regarded different.
  // If \a force_different is true, all keys are regarded different, resulting
  // in new subchannels always being created in a subchannel pool. Otherwise,
  // the keys will be compared as usual.
  //
  // Tests using this function \em MUST run tests with and without \a
  // force_different set.
  static void TestOnlySetForceDifferent(bool force_different);

 private:
  // Initializes the subchannel key with the given \a args and the function to
  // copy channel args.
  void Init(
      const grpc_subchannel_args* args,
      grpc_channel_args* (*copy_channel_args)(const grpc_channel_args* args));

  grpc_subchannel_args args_;
  // If set, all subchannel keys are regarded different.
  static bool force_different_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_KEY_H */
