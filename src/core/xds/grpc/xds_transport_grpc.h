//
// Copyright 2022 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_TRANSPORT_GRPC_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_TRANSPORT_GRPC_H

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/status.h>
#include <grpc/support/port_platform.h>
#include <grpc/transport_factory.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/sync.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/certificate_provider_store_interface.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_bootstrap.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class XdsClient;

// An XdsTransport that uses a gRPC channel.  It has no xDS-specific
// dependencies: the factory that creates it does all of the work with
// server targets and credential registries, and hands the transport
// everything it needs.
class GrpcXdsTransport final : public XdsTransport {
 public:
  // Wraps a channel that more than one transport can use.  Transports
  // that differ only in per-call settings (call creds, initial metadata,
  // and timeout) share the same SharedChannel.  Each transport factory
  // supplies its own implementation.
  class SharedChannel : public RefCounted<SharedChannel> {
   public:
    // Returns the underlying channel.  Never null.
    virtual Channel* channel() const = 0;

    // Returns the pollset_set to use for calls on the channel.  Must
    // stay valid while this object is alive.
    virtual grpc_pollset_set* interested_parties() const = 0;

    // Called when a transport that uses this channel is orphaned, so that
    // the owning factory can remove the transport from its cache.
    virtual void OnTransportOrphaned(absl::string_view key,
                                     GrpcXdsTransport* transport) = 0;
  };

  // key is an opaque cache key that is passed back to
  // SharedChannel::OnTransportOrphaned().
  GrpcXdsTransport(
      std::string key, RefCountedPtr<SharedChannel> channel,
      RefCountedPtr<grpc_call_credentials> call_creds,
      std::vector<std::pair<std::string, std::string>> initial_metadata,
      Duration timeout);
  ~GrpcXdsTransport() override;

  void Orphaned() override;

  void StartConnectivityFailureWatch(
      RefCountedPtr<ConnectivityFailureWatcher> watcher) override;
  void StopConnectivityFailureWatch(
      const RefCountedPtr<ConnectivityFailureWatcher>& watcher) override;

  OrphanablePtr<StreamingCall> CreateStreamingCall(
      const char* method,
      std::unique_ptr<StreamingCall::EventHandler> event_handler,
      CallOptions options) override;

  void ResetBackoff() override;

  Channel* channel() const;

 private:
  std::string key_;
  RefCountedPtr<SharedChannel> channel_;
  RefCountedPtr<grpc_call_credentials> call_creds_;
  std::vector<std::pair<std::string, std::string>> initial_metadata_;
  Duration timeout_;

  Mutex mu_;
  absl::flat_hash_map<RefCountedPtr<ConnectivityFailureWatcher>,
                      AsyncConnectivityStateWatcherInterface*>
      watchers_ ABSL_GUARDED_BY(&mu_);
};

class GrpcXdsTransportFactory final : public XdsTransportFactory {
 public:
  GrpcXdsTransportFactory(const ChannelArgs& args,
                          RefCountedPtr<CertificateProviderStoreInterface>
                              certificate_provider_store);
  ~GrpcXdsTransportFactory() override;

  void Orphaned() override {}

  RefCountedPtr<XdsTransport> GetTransport(
      const XdsBootstrap::XdsServerTarget& server,
      absl::Status* status) override;

  grpc_pollset_set* interested_parties() const { return interested_parties_; }

 private:
  class XdsSharedChannel;

  ChannelArgs args_;
  RefCountedPtr<CertificateProviderStoreInterface> certificate_provider_store_;
  grpc_pollset_set* interested_parties_;

  Mutex mu_;
  absl::flat_hash_map<std::string /*XdsServerTarget key*/, GrpcXdsTransport*>
      transports_ ABSL_GUARDED_BY(&mu_);
  absl::flat_hash_map<std::string /*Channel key*/, XdsSharedChannel*> channels_
      ABSL_GUARDED_BY(&mu_);
};

class GrpcStreamingCall final : public XdsTransport::StreamingCall {
 public:
  // Holds a ref to channel for the life of the call, which keeps the
  // channel and its pollset_set alive.
  GrpcStreamingCall(
      RefCountedPtr<GrpcXdsTransport::SharedChannel> channel,
      const char* method,
      std::unique_ptr<StreamingCall::EventHandler> event_handler,
      grpc_call_credentials* call_creds,
      const std::vector<std::pair<std::string, std::string>>& initial_metadata,
      Duration timeout, XdsTransport::CallOptions options);
  ~GrpcStreamingCall() override;

  void Orphan() override;

  void SendMessage(std::string payload, bool send_half_close) override;

  void StartRecvMessage() override;

  void SendHalfClose() override;

 private:
  using OpList = absl::InlinedVector<grpc_op, 3>;

  void AddSendInitialMetadataOp(OpList& op_list);
  void AddRecvInitialMetadataOp(OpList& op_list);
  void AddRecvTrailingMetadataOp(OpList& op_list);
  void AddSendCloseFromClientOp(OpList& op_list);
  void AddSendMessageOp(std::string payload, OpList& op_list);
  void StartBatch(const OpList& op_list, const char* ref_reason,
                  grpc_closure* closure);

  static void OnRecvInitialMetadata(void* arg, grpc_error_handle /*error*/);
  static void OnRequestSent(void* arg, grpc_error_handle error);
  static void OnHalfClosed(void* arg, grpc_error_handle error);
  static void OnResponseReceived(void* arg, grpc_error_handle /*error*/);
  static void OnStatusReceived(void* arg, grpc_error_handle /*error*/);

  RefCountedPtr<GrpcXdsTransport::SharedChannel> channel_;

  std::unique_ptr<StreamingCall::EventHandler> event_handler_;

  // Always non-NULL.
  grpc_call* call_;

  // recv_initial_metadata
  grpc_metadata_array initial_metadata_recv_;
  grpc_closure on_recv_initial_metadata_;

  // send_initial_metadata
  std::vector<grpc_metadata> send_initial_metadata_;
  bool sent_initial_metadata_ = false;

  // send_message
  grpc_byte_buffer* send_message_payload_ = nullptr;
  grpc_closure on_request_sent_;

  // half_close
  grpc_closure on_half_closed_;

  // recv_message
  grpc_byte_buffer* recv_message_payload_ = nullptr;
  grpc_closure on_response_received_;

  // recv_trailing_metadata
  grpc_metadata_array trailing_metadata_recv_;
  grpc_status_code status_code_;
  grpc_slice status_details_ = grpc_empty_slice();
  grpc_closure on_status_received_;

  const XdsTransport::CallOptions options_;
};

// The concrete experimental::TransportFactory::TransportHandle returned by the
// factories in this file.  Callers can DownCast to this type to get the
// XdsTransport.
class TransportImpl final
    : public experimental::TransportFactory::TransportHandle {
 public:
  explicit TransportImpl(RefCountedPtr<XdsTransport> transport)
      : transport_(std::move(transport)) {}

  const RefCountedPtr<XdsTransport>& transport() const { return transport_; }

 private:
  RefCountedPtr<XdsTransport> transport_;
};

// A TransportFactory for the xDS case.  Only the keys in the map are
// supported: an unknown key gets a lame transport.  Transports come from
// the XdsClient's existing GrpcXdsTransportFactory, so channels are shared
// with other xDS users.
class XdsTransportFactoryWrapper final : public experimental::TransportFactory {
 public:
  using TargetMap =
      absl::flat_hash_map<std::string /*key*/, GrpcXdsServerTarget>;

  // Gets the GrpcXdsTransportFactory from xds_client.
  static std::shared_ptr<experimental::TransportFactory> Create(
      const XdsClient& xds_client, TargetMap targets);

  XdsTransportFactoryWrapper(
      RefCountedPtr<GrpcXdsTransportFactory> transport_factory,
      TargetMap targets);

  std::unique_ptr<TransportHandle> CreateTransport(
      absl::string_view key) override;

 private:
  RefCountedPtr<GrpcXdsTransportFactory> transport_factory_;
  // Does not change after construction, so no lock is needed.
  const TargetMap targets_;
};

// Extracts TransportFactory from ChannelArgs. Returns nullptr if not set.
std::shared_ptr<experimental::TransportFactory>
GetTransportFactoryFromChannelArgs(const ChannelArgs& args);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_TRANSPORT_GRPC_H
