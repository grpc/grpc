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
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/gprpp/ref_counted.h"

namespace grpc_core {

class Subchannel;

extern TraceFlag grpc_subchannel_pool_trace;

// A key that can uniquely identify a subchannel.
class SubchannelKey {
 public:
  explicit SubchannelKey(const grpc_channel_args* args);
  ~SubchannelKey();

  // Copyable.
  SubchannelKey(const SubchannelKey& other);
  SubchannelKey& operator=(const SubchannelKey& other);
  // Movable
  SubchannelKey(SubchannelKey&&) noexcept;
  SubchannelKey& operator=(SubchannelKey&&) noexcept;

  // for use in absl::btree_map
  bool operator<(const SubchannelKey& other) const;

 private:
  // Initializes the subchannel key with the given \a args and the function to
  // copy channel args.
  void Init(
      const grpc_channel_args* args,
      grpc_channel_args* (*copy_channel_args)(const grpc_channel_args* args));

  const grpc_channel_args* args_;
};

// Interface for a class that holds a strong ref over a subchannel. The purpose
// of this class is to allow for subchannel pool implementations to control how
// subchannel strong refs are manipulated. For example, when using the global
// subchannel pool, we need to make sure that strong refs are
// manipulated atomically with pool registration and unregistration.
//
// Usage: \a RegisterSubchannel returns \a SubchannelRef
// object, and the destruction of a \a SubchannelRef arranges for any
// subchannel pool unregistration that might be needed.
//
// Note that while the wrapped \a Subchannel may be accessed via \a
// subchannel(), the wrapped \a Subchannel should never be Ref'd or Unref'd
// directly, i.e. the \a SubchannelRef itself is the only entity that should
// ever directly manipulate the subchannel's strong refs. It's fine to
// manipulate weak refs, however.
class SubchannelRef {
 public:
  virtual ~SubchannelRef() {}
  virtual Subchannel* subchannel() = 0;
};

// Interface for subchannel pool.
// TODO(juanlishen): This refcounting mechanism may lead to memory leak.
// To solve that, we should force polling to flush any pending callbacks, then
// shut down safely. See https://github.com/grpc/grpc/issues/12560.
class SubchannelPoolInterface : public RefCounted<SubchannelPoolInterface> {
 public:
  SubchannelPoolInterface()
      : RefCounted(GRPC_TRACE_FLAG_ENABLED(grpc_subchannel_pool_trace)
                       ? "SubchannelPoolInterface"
                       : nullptr) {}
  ~SubchannelPoolInterface() override {}

  // Registers a subchannel against a key. Returns the subchannel registered
  // with \a key, which may be different from \a constructed because we reuse
  // (instead of update) any existing subchannel already registered with \a key.
  virtual std::unique_ptr<SubchannelRef> RegisterSubchannel(
      const SubchannelKey& key, RefCountedPtr<Subchannel> constructed) = 0;

  // Creates a channel arg from \a subchannel pool.
  static grpc_arg CreateChannelArg(SubchannelPoolInterface* subchannel_pool);

  // Gets the subchannel pool from the channel args.
  static SubchannelPoolInterface* GetSubchannelPoolFromChannelArgs(
      const grpc_channel_args* args);
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_POOL_INTERFACE_H */
