/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_WRAPPER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_WRAPPER_H

#include <grpc/support/port_platform.h>

#include <ares.h>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/timer.h"
#include "src/core/lib/iomgr/work_serializer.h"

#define GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS 120000

extern grpc_core::TraceFlag grpc_trace_cares_address_sorting;

extern grpc_core::TraceFlag grpc_trace_cares_resolver;

#define GRPC_CARES_TRACE_LOG(format, ...)                           \
  do {                                                              \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_resolver)) {       \
      gpr_log(GPR_DEBUG, "(c-ares resolver) " format, __VA_ARGS__); \
    }                                                               \
  } while (0)

namespace grpc_core {

/// An AresRequest is a handle over a complete name resolution process
/// (A queries, AAAA queries, etc.). An AresRequest is created with a call
/// to LookupAresLocked, and it's safe to destroy as soon as the \a on_done
/// callback passed to LookupAresLocked has began to run. Meanwhile, a
/// name resolution process can be terminated abruptly by invoking \a
/// CancelLocked.
class AresRequest final {
 public:
  /// Callers should only create an AresRequest via \a LookupAresLocked, this
  /// ctor is made public only to help the factory method.
  AresRequest(
      std::unique_ptr<grpc_core::ServerAddressList>* addresses_out,
      std::unique_ptr<grpc_core::ServerAddressList>* balancer_addresses_out,
      absl::optional<std::string>* service_config_json_out,
      grpc_pollset_set* pollset_set, int query_timeout_ms,
      std::function<void(grpc_error*)> on_done,
      std::shared_ptr<grpc_core::WorkSerializer> work_serializer);

  static std::unique_ptr<AresRequest> Create(
      absl::string_view dns_server, absl::string_view name,
      absl::string_view default_port, grpc_pollset_set* interested_parties,
      std::function<void(grpc_error*)> on_done,
      std::unique_ptr<grpc_core::ServerAddressList>* addrs,
      std::unique_ptr<grpc_core::ServerAddressList>* balancer_addrs,
      absl::optional<std::string>* service_config_json, int query_timeout_ms,
      std::shared_ptr<grpc_core::WorkSerializer> work_serializer);

  /// Cancel the pending request. Must be called while holding the
  /// WorkSerializer that was used to call \a LookupAresLocked.
  void CancelLocked();

  /// Initialize the gRPC ares wrapper. Must be called at least once before
  /// ResolveAddressAres().
  static grpc_error* Init(void);

  /// Uninitialized the gRPC ares wrapper. If there was more than one previous
  /// call to AresInit(), this function uninitializes the gRPC ares
  /// wrapper only if it has been called the same number of times as
  /// AresInit().
  static void Shutdown(void);

 private:
  // An AresQuery tracks relevant state needed to perform one DNS query
  // type, within an overall DNS resolution. For example, there is one
  // AresQuery for A records, another for AAAA records, etc., each
  // instantiated as the proper subclass. The AresQuery base class manages
  // some book keeping on the relevant AresRequest that's needed to carry
  // out the overall resolution.
  class AresQuery {
   protected:
    explicit AresQuery(AresRequest* r);

    ~AresQuery();

    AresRequest* r_;
  };

  class AddressQuery : AresQuery {
   public:
    static void Create(AresRequest* r, const std::string& host, uint16_t port,
                       bool is_balancer, int address_family);

   private:
    AddressQuery(AresRequest* r, const std::string& host, uint16_t port,
                 bool is_balancer, int address_family);

    static void OnHostByNameDoneLocked(void* arg, int status, int timeouts,
                                       struct hostent* hostent);

    // host to resolve
    const std::string host_;
    // port to use in resulting socket addresses
    const uint16_t port_;
    // is it a grpclb address
    const bool is_balancer_;
    // for logging and errors: the query type ("A" or "AAAA")
    const char* qtype_;
    // the address family (AF_INET or AF_INET6)
    const int address_family_;
  };

  class SRVQuery : AresQuery {
   public:
    static void Create(AresRequest* r);

   private:
    explicit SRVQuery(AresRequest* r);

    static void OnSRVQueryDoneLocked(void* arg, int status, int timeouts,
                                     unsigned char* abuf, int alen);
  };

  class TXTQuery : AresQuery {
   public:
    static void Create(AresRequest* r);

   private:
    explicit TXTQuery(AresRequest* r);

    static void OnTXTDoneLocked(void* arg, int status, int timeouts,
                                unsigned char* buf, int len);
  };

  // An FdNode tracks an fd and its relevant state for polling it as
  // needed to carry out a c-ares resolution.
  class FdNode {
   public:
    FdNode(AresRequest* r,
           std::unique_ptr<grpc_core::GrpcPolledFd> grpc_polled_fd);

    ~FdNode();

    void MaybeRegisterForOnReadableLocked();

    void MaybeRegisterForOnWritableLocked();

    void MaybeShutdownLocked(const char* reason);

    bool IsActiveLocked();

    bool already_shutdown() { return already_shutdown_; }

    GrpcPolledFd* grpc_polled_fd() { return grpc_polled_fd_.get(); }

   private:
    void OnReadableLocked(grpc_error* error);

    static void OnReadable(void* arg, grpc_error* error);

    void OnWritableLocked(grpc_error* error);

    static void OnWritable(void* arg, grpc_error* error);

    AresRequest* r_;
    // a closure wrapping OnReadableLocked, which should be
    // invoked when the fd in this node becomes readable.
    grpc_closure read_closure_;
    // a closure wrapping OnWritableLocked, which should be
    // invoked when the fd in this node becomes writable.
    grpc_closure write_closure_;
    // wrapped fd that's polled by grpc's poller for the current platform
    std::unique_ptr<grpc_core::GrpcPolledFd> grpc_polled_fd_;
    // if the readable closure has been registered
    bool readable_registered_ = false;
    // if the writable closure has been registered
    bool writable_registered_ = false;
    // if the fd has been shutdown yet from grpc iomgr perspective
    bool already_shutdown_ = false;
  };

  grpc_millis CalculateNextAresBackupPollAlarm() const;

  void OnTimeoutLocked(grpc_error* error);

  static void OnTimeout(void* arg, grpc_error* error);

  void OnAresBackupPollAlarmLocked(grpc_error* error);

  static void OnAresBackupPollAlarm(void* arg, grpc_error* error);

  void NotifyOnEventLocked();

  void ContinueAfterCheckLocalhostAndIPLiteralsLocked(
      absl::string_view dns_server);

  void DecrementPendingQueries();

  void MaybeCallOnDoneLocked();

  bool ResolveAsIPLiteralLocked();

  bool MaybeResolveLocalHostManuallyLocked();

  std::string srv_qname() const {
    return absl::StrCat("_grpclb._tcp.", target_host_);
  }

  std::string txt_qname() const {
    return absl::StrCat("_grpc_config.", target_host_);
  }

  // the host component of the service name to resolve
  std::string target_host_;
  // the port component of the service name to resolve
  std::string target_port_;
  // the pointer to receive the resolved addresses
  std::unique_ptr<grpc_core::ServerAddressList>* addresses_out_;
  // the pointer to receive the resolved balancer addresses
  std::unique_ptr<grpc_core::ServerAddressList>* balancer_addresses_out_;
  // the pointer to receive the service config in JSON
  absl::optional<std::string>* service_config_json_out_;
  // the ares_channel owned by this request
  ares_channel channel_ = nullptr;
  // pollset set for driving the IO events of the channel
  grpc_pollset_set* pollset_set_;
  // work_serializer to synchronize c-ares and I/O callbacks on
  std::shared_ptr<grpc_core::WorkSerializer> work_serializer_;
  // Number of active DNS queries (one for A, another for AAAA, etc.).
  int pending_queries_ = 0;
  // the fds that this request is currently using.
  std::map<ares_socket_t, std::unique_ptr<FdNode>> fds_;
  // is this request being shut down
  bool shutting_down_ = false;
  // Owned by the ev_driver. Creates new GrpcPolledFd's
  std::unique_ptr<grpc_core::GrpcPolledFdFactory> polled_fd_factory_;
  // query timeout in milliseconds
  int query_timeout_ms_;
  // alarm to cancel active queries
  grpc_timer query_timeout_;
  // cancels queries on a timeout
  grpc_closure on_timeout_locked_;
  // whether or not on_timeout_locked has finished
  bool timeout_done_ = false;
  // alarm to poll ares_process on in case fd events don't happen
  grpc_timer ares_backup_poll_alarm_;
  // polls ares_process on a periodic timer
  grpc_closure on_ares_backup_poll_alarm_locked_;
  // whether or not the backup poller is done
  bool backup_poller_done_ = false;
  // callback to schedule when the request completes, empty means that
  // we've already scheduled the callback
  std::function<void(grpc_error*)> on_done_;
  // the errors explaining query failures, appended to in query callbacks
  grpc_error* error_ = GRPC_ERROR_NONE;
};

/// Asynchronously resolve \a name. Use \a default_port if a port isn't
/// designated in \a name, otherwise use the port in \a name.
/// AresInit() must be called at least once before this function.
extern void (*ResolveAddressAres)(const char* name, const char* default_port,
                                  grpc_pollset_set* interested_parties,
                                  grpc_closure* on_done,
                                  grpc_resolved_addresses** addresses);

/// Asynchronously resolve \a name. It will try to resolve grpclb SRV records in
/// addition to the normal address records if \a balancer_addresses is not
/// nullptr. For normal address records, it uses \a default_port if a port isn't
/// designated in \a name, otherwise it uses the port in \a name. AresInit()
/// must be called at least once before this function. The returned
/// AresRequest is safe to destroy after \a on_done is called back.
///
/// TODO(apolcyn): as a part of moving to new gRPC DNS API, remove the
/// work_serializer parameter and synchronize internally instead.
extern std::unique_ptr<AresRequest> (*LookupAresLocked)(
    absl::string_view dns_server, absl::string_view name,
    absl::string_view default_port, grpc_pollset_set* interested_parties,
    std::function<void(grpc_error*)> on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addresses,
    std::unique_ptr<grpc_core::ServerAddressList>* balancer_addresses,
    absl::optional<std::string>* service_config_json, int query_timeout_ms,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer);

/// Indicates whether or not AAAA queries should be attempted.
/// E.g., return false if ipv6 is known to not be available.
bool AresQueryIPv6();

/// Sorts destinations in \a addresses according to RFC 6724.
void AddressSortingSort(const AresRequest* r, ServerAddressList* addresses,
                        const std::string& logging_prefix);

namespace internal {

/// Exposed in this header for C-core tests only
extern void (*AresTestOnlyInjectConfig)(ares_channel channel);

}  // namespace internal

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_WRAPPER_H \
        */
