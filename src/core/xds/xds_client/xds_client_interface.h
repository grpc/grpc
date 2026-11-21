//
// Copyright 2026 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_CLIENT_INTERFACE_H
#define GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_CLIENT_INTERFACE_H

#include <memory>

#include "src/core/util/dual_ref_counted.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "src/core/xds/xds_client/xds_transport_factory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class XdsClientInterface : public DualRefCounted<XdsClientInterface> {
 public:
  class ReadDelayHandle : public RefCounted<ReadDelayHandle> {
   public:
    static RefCountedPtr<ReadDelayHandle> NoWait() { return nullptr; }
  };

  // Resource watcher interface.  Implemented by callers.
  // Note: Most callers will not use this API directly but rather via a
  // resource-type-specific wrapper API provided by the relevant
  // XdsResourceType implementation.
  class ResourceWatcherInterface : public RefCounted<ResourceWatcherInterface> {
   public:
    virtual void OnGenericResourceChanged(
        absl::StatusOr<std::shared_ptr<const XdsResourceType::ResourceData>>
            resource,
        RefCountedPtr<ReadDelayHandle> read_delay_handle) = 0;
    virtual void OnAmbientError(
        absl::Status status,
        RefCountedPtr<ReadDelayHandle> read_delay_handle) = 0;
  };

  // Start and cancel watch for a resource.
  //
  // The XdsClient takes ownership of the watcher, but the caller may
  // keep a raw pointer to the watcher, which may be used only for
  // cancellation.  (Because the caller does not own the watcher, the
  // pointer must not be used for any other purpose.)
  // If the caller is going to start a new watch after cancelling the
  // old one, it should set delay_unsubscription to true.
  //
  // The resource type object must be a global singleton, since the first
  // time the XdsClient sees a particular resource type object, it will
  // store the pointer to that object as the authoritative implementation for
  // its type URLs.  The resource type object must outlive the XdsClient object,
  // and it is illegal to start a subsequent watch for the same type URLs using
  // a different resource type object.
  //
  // Note: Most callers will not use this API directly but rather via a
  // resource-type-specific wrapper API provided by the relevant
  // XdsResourceType implementation.
  virtual void WatchResource(
      const XdsResourceType* type, absl::string_view name,
      RefCountedPtr<ResourceWatcherInterface> watcher) = 0;
  virtual void CancelResourceWatch(const XdsResourceType* type,
                                   absl::string_view name,
                                   ResourceWatcherInterface* watcher,
                                   bool delay_unsubscription) = 0;

  // Resets connection backoff state.
  virtual void ResetBackoff() = 0;

  virtual const XdsBootstrap& bootstrap() const = 0;

  virtual XdsTransportFactory* transport_factory() const = 0;

 protected:
  explicit XdsClientInterface(const char* trace = nullptr)
      : DualRefCounted<XdsClientInterface>(trace) {}
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_XDS_CLIENT_XDS_CLIENT_INTERFACE_H
