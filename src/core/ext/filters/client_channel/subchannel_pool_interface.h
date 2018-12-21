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

#include <grpc/impl/codegen/log.h>

//#include "src/core/ext/filters/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/avl/avl.h"
#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/ref_counted.h"

// Channel arg key for whether to use a local subchannel pool.
#define GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL "grpc.use_local_subchannel_pool"

typedef struct grpc_subchannel grpc_subchannel;
typedef struct grpc_subchannel_call grpc_subchannel_call;
typedef struct grpc_subchannel_args grpc_subchannel_args;

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

  grpc_subchannel_args* args_;
  // If set, all subchannel keys are regarded different.
  static bool force_different_;
};

// Interface for subchannel pool.
class SubchannelPoolInterface : public RefCounted<SubchannelPoolInterface> {
 public:
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

//  // Increments the (non-zero) refcount of the subchannel pool.
//  SubchannelPoolInterface* Ref();
//  // Decrements the refcount of the subchannel pool. If the refcount drops to
//  // zero, destroys the subchannel pool.
//  void Unref();

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  // The vtable for subchannel operations in an AVL tree.
  static const grpc_avl_vtable subchannel_avl_vtable_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_SUBCHANNEL_POOL_INTERFACE_H */
