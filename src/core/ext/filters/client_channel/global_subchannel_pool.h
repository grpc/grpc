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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_GLOBAL_SUBCHANNEL_POOL_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_GLOBAL_SUBCHANNEL_POOL_H

#include <grpc/support/port_platform.h>

#include <map>

#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {

// The global subchannel pool. It shares subchannels among channels. There
// should be only one instance of this class. Init() should be called once at
// the filter initialization time; Shutdown() should be called once at the
// filter shutdown time.
// TODO(juanlishen): Enable subchannel retention.
class GlobalSubchannelPool final : public SubchannelPoolInterface {
 public:
  // The ctor and dtor are not intended to use directly.
  GlobalSubchannelPool() {}
  ~GlobalSubchannelPool() override {}

  // Should be called exactly once at filter initialization time.
  static void Init();
  // Should be called exactly once at filter shutdown time.
  static void Shutdown();

  // Gets the singleton instance.
  static RefCountedPtr<GlobalSubchannelPool> instance();

  // Implements interface methods.
  RefCountedPtr<Subchannel> RegisterSubchannel(
      const SubchannelKey& key, RefCountedPtr<Subchannel> constructed) override
      ABSL_LOCKS_EXCLUDED(mu_);
  void UnregisterSubchannel(const SubchannelKey& key,
                            Subchannel* subchannel) override
      ABSL_LOCKS_EXCLUDED(mu_);
  RefCountedPtr<Subchannel> FindSubchannel(const SubchannelKey& key) override
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // The singleton instance. (It's a pointer to RefCountedPtr so that this
  // non-local static object can be trivially destructible.)
  static RefCountedPtr<GlobalSubchannelPool>* instance_;

  // A map from subchannel key to subchannel.
  std::map<SubchannelKey, Subchannel*> subchannel_map_ ABSL_GUARDED_BY(mu_);
  // To protect subchannel_map_.
  Mutex mu_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_GLOBAL_SUBCHANNEL_POOL_H */
