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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_POOL_INTERFACE_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_POOL_INTERFACE_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/avl/avl.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/ref_counted.h"

struct grpc_subchannel;

namespace grpc_core {

extern TraceFlag grpc_subchannel_pool_trace;

// A key that can uniquely identify a subchannel.
class SubchannelKey {
 public:
  explicit SubchannelKey(const grpc_channel_args* args);
  ~SubchannelKey();

  // Copyable.
  SubchannelKey(const SubchannelKey& other);
  SubchannelKey& operator=(const SubchannelKey& other);
  // Not movable.
  SubchannelKey(SubchannelKey&&) = delete;
  SubchannelKey& operator=(SubchannelKey&&) = delete;

  int Cmp(const SubchannelKey& other) const;

  // Sets whether subchannel keys are always initialized differently in the
  // normal ctor. If \a force_different is true, all the subchannel keys
  // constructed via the normal ctor (vs the copy ctor) will be different even
  // if the ctor arguments are the same, resulting in new subchannels always
  // being created in a subchannel pool. Otherwise, the subchannel keys will be
  // initialized as usual.
  //
  // Tests using this function \em MUST run tests with and without \a
  // force_different set.
  static void TestOnlySetForceDifferent(bool force_different);

 private:
  // Initializes the subchannel key with the given \a args and the function to
  // copy channel args.
  void Init(
      const grpc_channel_args* args,
      grpc_channel_args* (*copy_channel_args)(const grpc_channel_args* args));

  const grpc_channel_args* args_;

  // Additional field to differentiate each subchannel key in testing.
  size_t test_only_id_;

  // If set, all subchannel keys are initialized differently (except for the
  // copies of the same subchannel key).
  static gpr_atm force_different_;
};

// Interface for subchannel pool.
// TODO(juanlishen): This refcounting mechanism may lead to memory leak.
// To solve that, we should force polling to flush any pending callbacks, then
// shut down safely. See https://github.com/grpc/grpc/issues/12560.
class SubchannelPoolInterface : public RefCounted<SubchannelPoolInterface> {
 public:
  SubchannelPoolInterface() : RefCounted(&grpc_subchannel_pool_trace) {}
  virtual ~SubchannelPoolInterface() {}

  // Registers a subchannel against a key. Takes ownership of \a constructed.
  // Returns the registered subchannel, which may be different from \a
  // constructed in the case of a registration race.
  virtual grpc_subchannel* RegisterSubchannel(
      SubchannelKey* key, grpc_subchannel* constructed) GRPC_ABSTRACT;

  // Removes \a constructed as the registered subchannel for \a key. Does
  // nothing if \a key no longer refers to \a constructed.
  virtual void UnregisterSubchannel(SubchannelKey* key,
                                    grpc_subchannel* constructed) GRPC_ABSTRACT;

  // Finds the subchannel registered for the given subchannel key. Returns NULL
  // if no such channel exists. Thread-safe.
  virtual grpc_subchannel* FindSubchannel(SubchannelKey* key) GRPC_ABSTRACT;

  GRPC_ABSTRACT_BASE_CLASS
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_POOL_INTERFACE_H */
