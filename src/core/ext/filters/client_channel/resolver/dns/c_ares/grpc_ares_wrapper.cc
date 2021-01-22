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

#include <grpc/support/port_platform.h>

#if GRPC_ARES == 1

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>
#include <sys/types.h>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <ares.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include <address_sorting/address_sorting.h>
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/nameser.h"
#include "src/core/lib/iomgr/parse_address.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/transport/authority_override.h"

using grpc_core::ServerAddress;
using grpc_core::ServerAddressList;

grpc_core::TraceFlag grpc_trace_cares_address_sorting(false,
                                                      "cares_address_sorting");

grpc_core::TraceFlag grpc_trace_cares_resolver(false, "cares_resolver");

namespace grpc_core {

AresRequest::AresRequest(
    std::unique_ptr<grpc_core::ServerAddressList>* addresses_out,
    std::unique_ptr<grpc_core::ServerAddressList>* balancer_addresses_out,
    std::unique_ptr<std::string>* service_config_json_out,
    grpc_pollset_set* pollset_set, int query_timeout_ms,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer)
    : addresses_out_(addresses_out),
      balancer_addresses_out_(balancer_addresses_out),
      service_config_json_out_(service_config_json_out),
      pollset_set_(pollset_set),
      work_serializer_(std::move(work_serializer)),
      polled_fd_factory_(NewGrpcPolledFdFactory(work_serializer_)),
      query_timeout_ms_(query_timeout_ms) {}

AresRequest::OnDoneScheduler::OnDoneScheduler(
    AresRequest* r, std::function<void(grpc_error*)> on_done)
    : parent_(r), on_done_(on_done) {}

void AresRequest::OnDoneScheduler::Orphan() {
  // After setting shutting_down_ = true, NotifyOnEventLocked will
  // shut down any remaining fds.
  // TODO(apolcyn): just run CancelLocked() ?
  GRPC_CARES_TRACE_LOG("request: %p OnDoneScheduler orphaned", parent());
  parent()->shutting_down_ = true;
  grpc_timer_cancel(&parent()->query_timeout_);
  grpc_timer_cancel(&parent()->ares_backup_poll_alarm_);
}

AresRequest::OnDoneScheduler::~OnDoneScheduler() {
  GRPC_CARES_TRACE_LOG("request: %p ~OnDoneScheduler", parent());
  GPR_ASSERT(parent()->fds_.size() == 0);
  if (parent()->channel_ != nullptr) {
    ares_destroy(parent()->channel_);
  }
  ServerAddressList* addresses = parent()->addresses_out_->get();
  if (addresses != nullptr) {
    AddressSortingSort(parent(), addresses, "service-addresses");
    GRPC_ERROR_UNREF(parent()->error_);
    parent()->error_ = GRPC_ERROR_NONE;
    // TODO(apolcyn): allow c-ares to return a service config
    // with no addresses along side it
  }
  if (parent()->balancer_addresses_out_ != nullptr) {
    ServerAddressList* balancer_addresses =
        parent()->balancer_addresses_out_->get();
    if (balancer_addresses != nullptr) {
      AddressSortingSort(parent(), balancer_addresses, "grpclb-addresses");
    }
  }
  grpc_error* error = parent()->error_;
  std::function<void(grpc_error*)> on_done = on_done_;
  parent()->work_serializer_->Run(
      [on_done, error]() {
        on_done(error);
        GRPC_ERROR_UNREF(error);
      },
      DEBUG_LOCATION);
}

AresRequest::AresQuery(RefCountedPtr<AresRequest::OnDoneScheduler o) : o_(std::move(o)) {}

AresRequest::AddressQuery::AddressQuery(RefCountedPtr<AresRequest::OnDoneScheduler> o,
                        const std::string& host, uint16_t port,
                        bool is_balancer, int address_family) : AresQuery(std::move(o)), host_(host), port_(port), is_balancer_(is_balancer), address_family_(address_family) {
    ares_gethostbyname(o_->parent()->channel_, host_.c_str(), address_family_,
                       AresRequest::OnHostByNameDoneLocked, this);
}

AresRequest::SRVQuery::SRVQuery(RefCountedPtr<AresRequest::OnDoneScheduler> o) : AresQuery(std::move(o)) {
    ares_query(o_->parent()->channel_, o_->parent()->srv_qname().c_str(), ns_c_in, ns_t_srv,
               AresRequest::OnSRVQueryDoneLocked, this);
}

AresRequest::TXTQuery::TXTQuery(RefCountedPtr<AresRequest::OnDoneScheduler> o) : AresQuery(std::move(o)) {
    ares_search(o_->parent()->channel_, o_->parent()->txt_qname().c_str(), ns_c_in, ns_t_txt,
                AresQuery::OnTXTDoneLocked, this);
}

void AresRequest::AddressQuery::OnHostByNameDoneLocked(void* arg, int status,
                                       int /*timeouts*/,
                                       struct hostent* hostent) {
  std::unique_ptr<AresRequest::AddressQuery> q(
      static_cast<AresRequest::AddressQuery*>(arg));
  AresRequest* r = q->o_->parent();
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG(
        "request:%p on_hostbyname_done_locked qtype=%s host=%s ARES_SUCCESS", r,
        q->qtype_, q->host_.c_str());
    std::unique_ptr<ServerAddressList>* address_list_ptr =
        q->is_balancer_ ? r->balancer_addresses_out_ : r->addresses_out_;
    if (*address_list_ptr == nullptr) {
      *address_list_ptr = absl::make_unique<ServerAddressList>();
    }
    ServerAddressList& addresses = **address_list_ptr;
    for (size_t i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
      absl::InlinedVector<grpc_arg, 1> args_to_add;
      if (q->is_balancer_) {
        args_to_add.emplace_back(
            grpc_core::CreateAuthorityOverrideChannelArg(q->host_.c_str()));
      }
      grpc_channel_args* args = grpc_channel_args_copy_and_add(
          nullptr, args_to_add.data(), args_to_add.size());
      switch (hostent->h_addrtype) {
        case AF_INET6: {
          size_t addr_len = sizeof(struct sockaddr_in6);
          struct sockaddr_in6 addr;
          memset(&addr, 0, addr_len);
          memcpy(&addr.sin6_addr, hostent->h_addr_list[i],
                 sizeof(struct in6_addr));
          addr.sin6_family = static_cast<unsigned char>(hostent->h_addrtype);
          addr.sin6_port = q->port_;
          addresses.emplace_back(&addr, addr_len, args);
          char output[INET6_ADDRSTRLEN];
          ares_inet_ntop(AF_INET6, &addr.sin6_addr, output, INET6_ADDRSTRLEN);
          GRPC_CARES_TRACE_LOG(
              "request:%p c-ares resolver gets a AF_INET6 result: \n"
              "  addr: %s\n  port: %d\n  sin6_scope_id: %d\n",
              r, output, ntohs(q->port_), addr.sin6_scope_id);
          break;
        }
        case AF_INET: {
          size_t addr_len = sizeof(struct sockaddr_in);
          struct sockaddr_in addr;
          memset(&addr, 0, addr_len);
          memcpy(&addr.sin_addr, hostent->h_addr_list[i],
                 sizeof(struct in_addr));
          addr.sin_family = static_cast<unsigned char>(hostent->h_addrtype);
          addr.sin_port = q->port_;
          addresses.emplace_back(&addr, addr_len, args);
          char output[INET_ADDRSTRLEN];
          ares_inet_ntop(AF_INET, &addr.sin_addr, output, INET_ADDRSTRLEN);
          GRPC_CARES_TRACE_LOG(
              "request:%p c-ares resolver gets a AF_INET result: \n"
              "  addr: %s\n  port: %d\n",
              r, output, ntohs(q->port_));
          break;
        }
      }
    }
  } else {
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=%s name=%s is_balancer=%d: %s",
        q->qtype_, q->host_.c_str(), q->is_balancer_, ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_hostbyname_done_locked: %s", r,
                         error_msg.c_str());
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg.c_str());
    r->error_ = grpc_error_add_child(error, r->error_);
  }
}

void AresRequest::SRVQuery::OnSRVQueryDoneLocked(void* arg, int status, int /*timeouts*/,
                                       unsigned char* abuf, int alen) {

  std::unique_ptr<AresRequest::SRVQuery> q(
      static_cast<AresRequest::SRVQuery*>(arg));
  AresRequest* r = q->o_->parent();
  if (status == ARES_SUCCESS) {
    GRPC_CARES_TRACE_LOG(
        "request:%p on_srv_query_done_locked name=%s ARES_SUCCESS", r,
        r->srv_qname().c_str());
    struct ares_srv_reply* reply;
    const int parse_status = ares_parse_srv_reply(abuf, alen, &reply);
    GRPC_CARES_TRACE_LOG("request:%p ares_parse_srv_reply: %d", r,
                         parse_status);
    if (parse_status == ARES_SUCCESS) {
      for (struct ares_srv_reply* srv_it = reply; srv_it != nullptr;
           srv_it = srv_it->next) {
        if (AresQueryIPv6()) {
          // deletes itself
          new AresRequest::AddressQuery(
              o, std::string(srv_it->host), htons(srv_it->port),
              true /* is_balancer */, AF_INET6 /* address_family */);
        }
        // deletes itself
        new AresRequest::AddressQuery(
            o, std::string(srv_it->host), htons(srv_it->port),
            true /* is_balancer */, AF_INET /* address_family */);
        r->NotifyOnEventLocked(o->WeakRef());
      }
    }
    if (reply != nullptr) {
      ares_free_data(reply);
    }
  } else {
    std::string error_msg = absl::StrFormat(
        "C-ares status is not ARES_SUCCESS qtype=SRV name=%s: %s",
        r->srv_qname().c_str(), ares_strerror(status));
    GRPC_CARES_TRACE_LOG("request:%p on_srv_query_done_locked: %s", r,
                         error_msg.c_str());
    grpc_error* error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg.c_str());
    r->error_ = grpc_error_add_child(error, r->error_);
  }
}

void AresQuery::OnTXTDoneLocked(void* arg, int status, int /*timeouts*/,
                                  unsigned char* buf, int len) {
  std::unique_ptr<AresRequest::TXTQuery> q(
      static_cast<AresRequest::TXTQuery*>(arg));
  AresRequest* r = q->o_->parent();
  const std::string kServiceConfigAttributePrefix = "grpc_config=";
  struct ares_txt_ext* result = nullptr;
  struct ares_txt_ext* reply = nullptr;
  grpc_error* error = GRPC_ERROR_NONE;
  if (status != ARES_SUCCESS) goto fail;
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked name=%s ARES_SUCCESS", r,
                       r->txt_qname().c_str());
  status = ares_parse_txt_reply_ext(buf, len, &reply);
  if (status != ARES_SUCCESS) goto fail;
  // Find service config in TXT record.
  for (result = reply; result != nullptr; result = result->next) {
    if (result->record_start &&
        memcmp(result->txt, kServiceConfigAttributePrefix.data(),
               kServiceConfigAttributePrefix.size()) == 0) {
      break;
    }
  }
  // Found a service config record.
  if (result != nullptr) {
    std::string first_chunk =
        std::string(reinterpret_cast<const char*>(result->txt), result->length)
            .substr(kServiceConfigAttributePrefix.size());
    *r->service_config_json_out_ = absl::make_unique<std::string>(first_chunk);
    for (result = result->next; result != nullptr && !result->record_start;
         result = result->next) {
      std::string next_chunk(reinterpret_cast<const char*>(result->txt),
                             result->length);
      *r->service_config_json_out_ = absl::make_unique<std::string>(
          absl::StrCat(*r->service_config_json_out_->get(), next_chunk));
    }
    GRPC_CARES_TRACE_LOG("request:%p found service config: %s", r,
                         r->service_config_json_out_->get()->c_str());
  }
  // Clean up.
  ares_free_data(reply);
  return;
fail:
  std::string error_msg =
      absl::StrFormat("C-ares status is not ARES_SUCCESS qtype=TXT name=%s: %s",
                      r->txt_qname(), ares_strerror(status));
  error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg.c_str());
  GRPC_CARES_TRACE_LOG("request:%p on_txt_done_locked %s", r,
                       error_msg.c_str());
  r->error_ = grpc_error_add_child(error, r->error_);
}

AresRequest::FdNode::FdNode(
    WeakRefCountedPtr<AresRequest::OnDoneScheduler> o,
    std::unique_ptr<grpc_core::GrpcPolledFd> grpc_polled_fd)
    : o_(std::move(o)), grpc_polled_fd_(std::move(grpc_polled_fd)) {
  GRPC_CARES_TRACE_LOG("request:%p new fd: %s", o_->parent(),
                       grpc_polled_fd_->GetName());
  GRPC_CLOSURE_INIT(&read_closure_, AresRequest::FdNode::OnReadable, this,
                    grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&write_closure_, AresRequest::FdNode::OnWritable, this,
                    grpc_schedule_on_exec_ctx);
}

AresRequest::FdNode::~FdNode() {
  GRPC_CARES_TRACE_LOG("request:%p delete fd: %s", o_->parent(),
                       grpc_polled_fd_->GetName());
  GPR_ASSERT(!readable_registered_);
  GPR_ASSERT(!writable_registered_);
  GPR_ASSERT(already_shutdown_);
}

void AresRequest::FdNode::MaybeRegisterForOnReadableLocked() {
  if (!readable_registered_) {
    GRPC_CARES_TRACE_LOG("request:%p notify read on: %s", o_->parent(),
                         grpc_polled_fd_->GetName());
    grpc_polled_fd_->RegisterForOnReadableLocked(&read_closure_);
    readable_registered_ = true;
  }
}

void AresRequest::FdNode::MaybeRegisterForOnWritableLocked() {
  if (!writable_registered_) {
    GRPC_CARES_TRACE_LOG("request:%p notify write on: %s", o_->parent(),
                         grpc_polled_fd_->GetName());
    grpc_polled_fd_->RegisterForOnWriteableLocked(&read_closure_);
    writable_registered_ = true;
  }
}

void AresRequest::FdNode::MaybeShutdownLocked(const char* reason) {
  if (!already_shutdown_) {
    already_shutdown_ = true;
    grpc_polled_fd_->ShutdownLocked(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING(reason));
  }
}

bool AresRequest::FdNode::IsActiveLocked() {
  return readable_registered_ || writable_registered_;
}

grpc_millis AresRequest::CalculateNextAresBackupPollAlarm() const {
  // An alternative here could be to use ares_timeout to try to be more
  // accurate, but that would require using "struct timeval"'s, which just makes
  // things a bit more complicated. So just poll every second, as suggested
  // by the c-ares code comments.
  grpc_millis kMsUntilNextAresBackupPollAlarm = 1000;
  GRPC_CARES_TRACE_LOG(
      "request:%p next ares process poll time in "
      "%" PRId64 " ms",
      this, kMsUntilNextAresBackupPollAlarm);
  return kMsUntilNextAresBackupPollAlarm + grpc_core::ExecCtx::Get()->Now();
}

void AresRequest::OnTimeoutLocked(
    WeakRefCountedPtr<AresRequest::OnDoneScheduler> o, grpc_error* error) {
  GRPC_CARES_TRACE_LOG(
      "request:%p OnTimeoutLocked. shutting_down_=%d. "
      "err=%s",
      this, shutting_down_, grpc_error_string(error));
  // TODO(apolcyn): always run CancelLocked since it's idempotent?
  if (!shutting_down_ && error == GRPC_ERROR_NONE) {
    CancelLocked();
  }
  GRPC_ERROR_UNREF(error);
}

void AresRequest::OnTimeout(void* arg, grpc_error* error) {
  AresRequest::OnDoneScheduler* o =
      static_cast<AresRequest::OnDoneScheduler*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  o->parent()->work_serializer_->Run(
      [o, error]() {
        o->parent()->OnTimeoutLocked(
            WeakRefCountedPtr<AresRequest::OnDoneScheduler>(o), error);
      },
      DEBUG_LOCATION);
}

// In case of non-responsive DNS servers, dropped packets, etc., c-ares has
// intelligent timeout and retry logic, which we can take advantage of by
// polling ares_process_fd on time intervals. Overall, the c-ares library is
// meant to be called into and given a chance to proceed name resolution:
//   a) when fd events happen
//   b) when some time has passed without fd events having happened
// For the latter, we use this backup poller. Also see
// https://github.com/grpc/grpc/pull/17688 description for more details.
void AresRequest::OnAresBackupPollAlarmLocked(
    WeakRefCountedPtr<AresRequest::OnDoneScheduler> o, grpc_error* error) {
  GRPC_CARES_TRACE_LOG(
      "request:%p OnAresBackupPollAlarmLocked. "
      "shutting_down_=%d. "
      "err=%s",
      this, shutting_down_, grpc_error_string(error));
  if (!shutting_down_ && error == GRPC_ERROR_NONE) {
    for (auto& it : fds_) {
      AresRequest::FdNode* fdn = it.second.get();
      if (!fdn->already_shutdown()) {
        GRPC_CARES_TRACE_LOG(
            "request:%p OnAresBackupPollAlarmLocked; "
            "ares_process_fd. fd=%s",
            this, fdn->grpc_polled_fd()->GetName());
        ares_socket_t as = fdn->grpc_polled_fd()->GetWrappedAresSocketLocked();
        ares_process_fd(channel_, as, as);
      }
    }
    // the work done in ares_process_fd might have set shutting_down_ = true
    if (!shutting_down_) {
      grpc_millis next_ares_backup_poll_alarm =
          CalculateNextAresBackupPollAlarm();
      o->WeakRef().release();  // owned by timer callback
      grpc_timer_init(&ares_backup_poll_alarm_, next_ares_backup_poll_alarm,
                      &on_ares_backup_poll_alarm_locked_);
    }
    NotifyOnEventLocked(o);
  }
  GRPC_ERROR_UNREF(error);
}

void AresRequest::OnAresBackupPollAlarm(void* arg, grpc_error* error) {
  AresRequest::OnDoneScheduler* o =
      static_cast<AresRequest::OnDoneScheduler*>(arg);
  GRPC_ERROR_REF(error);
  o->parent()->work_serializer_->Run(
      [o, error]() {
        o->parent()->OnAresBackupPollAlarmLocked(
            WeakRefCountedPtr<AresRequest::OnDoneScheduler>(o), error);
      },
      DEBUG_LOCATION);
}

void AresRequest::FdNode::OnReadableLocked(grpc_error* error) {
  AresRequest* r = o_->parent();
  GPR_ASSERT(readable_registered_);
  readable_registered_ = false;
  const ares_socket_t as = grpc_polled_fd_->GetWrappedAresSocketLocked();
  GRPC_CARES_TRACE_LOG("request:%p readable on %s", r,
                       grpc_polled_fd_->GetName());
  if (error == GRPC_ERROR_NONE) {
    do {
      ares_process_fd(r->channel_, as, ARES_SOCKET_BAD);
    } while (grpc_polled_fd_->IsFdStillReadableLocked());
  } else {
    // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
    // timed out. The pending lookups made on this request will be cancelled
    // by the following ares_cancel() and the on_done callbacks will be invoked
    // with a status of ARES_ECANCELLED. The remaining file descriptors in this
    // request will be cleaned up in the follwing
    // NotifyOnEventLocked().
    ares_cancel(r->channel_);
  }
  r->NotifyOnEventLocked(o_);
  GRPC_ERROR_UNREF(error);
}

void AresRequest::FdNode::OnReadable(void* arg, grpc_error* error) {
  AresRequest::FdNode* fdn = static_cast<AresRequest::FdNode*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  fdn->o_->parent()->work_serializer_->Run(
      [fdn, error]() { fdn->OnReadableLocked(error); }, DEBUG_LOCATION);
}

void AresRequest::FdNode::OnWritableLocked(grpc_error* error) {
  AresRequest* r = o_->parent();
  GPR_ASSERT(writable_registered_);
  writable_registered_ = false;
  const ares_socket_t as = grpc_polled_fd_->GetWrappedAresSocketLocked();
  GRPC_CARES_TRACE_LOG("request:%p writable on %s", r,
                       grpc_polled_fd_->GetName());
  if (error == GRPC_ERROR_NONE) {
    ares_process_fd(r->channel_, ARES_SOCKET_BAD, as);
  } else {
    // If error is not GRPC_ERROR_NONE, it means the fd has been shutdown or
    // timed out. The pending lookups made on this request will be cancelled
    // by the following ares_cancel() and the on_done callbacks will be invoked
    // with a status of ARES_ECANCELLED. The remaining file descriptors in this
    // request will be cleaned up in the follwing NotifyOnEventLocked().
    ares_cancel(r->channel_);
  }
  r->NotifyOnEventLocked(o_);
  GRPC_ERROR_UNREF(error);
}

void AresRequest::FdNode::OnWritable(void* arg, grpc_error* error) {
  AresRequest::FdNode* fdn = static_cast<AresRequest::FdNode*>(arg);
  GRPC_ERROR_REF(error);  // ref owned by lambda
  fdn->o_->parent()->work_serializer_->Run(
      [fdn, error]() { fdn->OnWritableLocked(error); }, DEBUG_LOCATION);
}

// Get the file descriptors used by the request's ares channel, register
// I/O readable/writable callbacks with these filedescriptors.
void AresRequest::NotifyOnEventLocked(
    WeakRefCountedPtr<AresRequest::OnDoneScheduler> o) {
  AresRequest::FdNode* new_list = nullptr;
  std::map<ares_socket_t, std::unique_ptr<AresRequest::FdNode>> active_fds;
  if (!shutting_down_) {
    ares_socket_t socks[ARES_GETSOCK_MAXNUM];
    int socks_bitmask = ares_getsock(channel_, socks, ARES_GETSOCK_MAXNUM);
    for (size_t i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      if (ARES_GETSOCK_READABLE(socks_bitmask, i) ||
          ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
        if (fds_[socks[i]] == nullptr) {
          // Create a new FdNode if sock[i] is not in the FdNode list.
          fds_[socks[i]] = absl::make_unique<AresRequest::FdNode>(
              o, std::unique_ptr<GrpcPolledFd>(
                     polled_fd_factory_->NewGrpcPolledFdLocked(
                         socks[i], pollset_set_, work_serializer_)));
        }
        auto it = fds_.find(socks[i]);
        // Register read_closure if the socket is readable and read_closure has
        // not been registered with this socket.
        if (ARES_GETSOCK_READABLE(socks_bitmask, i)) {
          it->second->MaybeRegisterForOnReadableLocked();
        }
        // Register write_closure if the socket is writable and write_closure
        // has not been registered with this socket.
        if (ARES_GETSOCK_WRITABLE(socks_bitmask, i)) {
          it->second->MaybeRegisterForOnWritableLocked();
        }
        active_fds[i] = std::move(it->second);
        fds_.erase(it);
      }
    }
  }
  // Any remaining fds in fds_ were not returned by ares_getsock() and
  // are therefore no longer in use, so they can be shut down and removed from
  // the list.
  for (auto& it : fds_) {
    it.second->MaybeShutdownLocked("c-ares fd shutdown");
    if (it.second->IsActiveLocked()) {
      active_fds[it.first] = std::move(it.second);
    }
  }
  fds_ = std::move(active_fds);
}

namespace internal {

namespace {

void NoopInjectChannelConfig(ares_channel /*channel*/) {}

}  // namespace

void (*AresTestOnlyInjectConfig)(ares_channel channel) =
    NoopInjectChannelConfig;

}  // namespace internal

static void LogAddressSortingList(const AresRequest* r,
                                  const ServerAddressList& addresses,
                                  const char* input_output_str) {
  for (size_t i = 0; i < addresses.size(); i++) {
    std::string addr_str =
        grpc_sockaddr_to_string(&addresses[i].address(), true);
    gpr_log(GPR_INFO,
            "(c-ares resolver) request:%p c-ares address sorting: %s[%" PRIuPTR
            "]=%s",
            r, input_output_str, i, addr_str.c_str());
  }
}

void AddressSortingSort(const AresRequest* r, ServerAddressList* addresses,
                        const std::string& logging_prefix) {
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_address_sorting)) {
    LogAddressSortingList(r, *addresses, absl::StrCat(logging_prefix, "-input").c_str());
  }
  std::vector<address_sorting_sortable> sortables;
  sortables.resize(addresses->size());
  for (size_t i = 0; i < addresses->size(); ++i) {
    sortables[i].user_data = &(*addresses)[i];
    memcpy(&sortables[i].dest_addr.addr, &(*addresses)[i].address().addr,
           (*addresses)[i].address().len);
    sortables[i].dest_addr.len = (*addresses)[i].address().len;
  }
  address_sorting_rfc_6724_sort(sortables.data(), addresses->size());
  ServerAddressList sorted;
  sorted.reserve(addresses->size());
  for (size_t i = 0; i < addresses->size(); ++i) {
    sorted.emplace_back(*static_cast<ServerAddress*>(sortables[i].user_data));
  }
  *addresses = std::move(sorted);
  if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_address_sorting)) {
    LogAddressSortingList(r, *addresses, absl::StrCat(logging_prefix, "-output").c_str());
  }
}

void AresRequest::ContinueAfterCheckLocalhostAndIPLiteralsLocked(
    RefCountedPtr<AresRequest::OnDoneScheduler> o,
    absl::string_view dns_server) {
  ares_options opts;
  memset(&opts, 0, sizeof(opts));
  opts.flags |= ARES_FLAG_STAYOPEN;
  int status = ares_init_options(&channel_, &opts, ARES_OPT_FLAGS);
  internal::AresTestOnlyInjectConfig(channel_);
  if (status != ARES_SUCCESS) {
    error_ = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Failed to init ares channel. C-ares error: ",
                     ares_strerror(status))
            .c_str());
    return;
  }
  // If dns_server is specified, use it.
  if (!dns_server.empty()) {
    GRPC_CARES_TRACE_LOG("request:%p Using DNS server %s", this,
                         std::string(dns_server).c_str());
    struct ares_addr_port_node dns_server_addr;
    memset(&dns_server_addr, 0, sizeof(dns_server_addr));
    grpc_resolved_address addr;
    if (grpc_parse_ipv4_hostport(dns_server, &addr, false /* log_errors */)) {
      dns_server_addr.family = AF_INET;
      struct sockaddr_in* in = reinterpret_cast<struct sockaddr_in*>(addr.addr);
      memcpy(&dns_server_addr.addr.addr4, &in->sin_addr,
             sizeof(struct in_addr));
      dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else if (grpc_parse_ipv6_hostport(dns_server, &addr,
                                        false /* log_errors */)) {
      dns_server_addr.family = AF_INET6;
      struct sockaddr_in6* in6 =
          reinterpret_cast<struct sockaddr_in6*>(addr.addr);
      memcpy(&dns_server_addr.addr.addr6, &in6->sin6_addr,
             sizeof(struct in6_addr));
      dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else {
      error_ = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("cannot parse DNS server ip address: ", dns_server)
              .c_str());
      return;
    }
    int status = ares_set_servers_ports(channel_, &dns_server_addr);
    if (status != ARES_SUCCESS) {
      error_ = GRPC_ERROR_CREATE_FROM_COPIED_STRING(
          absl::StrCat("C-ares status is not ARES_SUCCESS: ",
                       ares_strerror(status))
              .c_str());
      return;
    }
  }
  if (AresQueryIPv6()) {
    // deletes itself
    new AresRequest::AddressQuery(
        o, target_host_, grpc_strhtons(target_port_.c_str()),
        false /* is_balancer */, AF_INET6 /* address_family */);
  }
  // deletes itself
  new AresRequest::AddressQuery(
      o, target_host_, grpc_strhtons(target_port_.c_str()),
      false /* is_balancer */, AF_INET /* address_family */);
  if (balancer_addresses_out_ != nullptr) {
    new AresRequest::SRVQuery(o); // deletes itself
  }
  if (service_config_json_out_ != nullptr) {
    new AresRequest::TXTQuery(o); // deletes itself
  }
  NotifyOnEventLocked(o->WeakRef());
}

bool AresRequest::ResolveAsIPLiteralLocked() {
  grpc_resolved_address addr;
  std::string hostport =
      grpc_core::JoinHostPort(target_host_, atoi(target_port_.c_str()));
  if (grpc_parse_ipv4_hostport(hostport.c_str(), &addr,
                               false /* log errors */) ||
      grpc_parse_ipv6_hostport(hostport.c_str(), &addr,
                               false /* log errors */)) {
    GPR_ASSERT(*addresses_out_ == nullptr);
    *addresses_out_ = absl::make_unique<ServerAddressList>();
    (*addresses_out_)->emplace_back(addr.addr, addr.len, nullptr /* args */);
    return true;
  }
  return false;
}

#ifdef GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY
bool AresRequest::MaybeResolveLocalHostManuallyLocked() {
  if (target_host_ == "localhost") {
    GPR_ASSERT(*addresses_out_ == nullptr);
    *addresses_out_ = absl::make_unique<grpc_core::ServerAddressList>();
    uint16_t numeric_port = grpc_strhtons(target_port_.c_str());
    // Append the ipv6 loopback address.
    struct sockaddr_in6 ipv6_loopback_addr;
    memset(&ipv6_loopback_addr, 0, sizeof(ipv6_loopback_addr));
    ((char*)&ipv6_loopback_addr.sin6_addr)[15] = 1;
    ipv6_loopback_addr.sin6_family = AF_INET6;
    ipv6_loopback_addr.sin6_port = numeric_port;
    (*addresses_out_)
        ->emplace_back(&ipv6_loopback_addr, sizeof(ipv6_loopback_addr),
                       nullptr /* args */);
    // Append the ipv4 loopback address.
    struct sockaddr_in ipv4_loopback_addr;
    memset(&ipv4_loopback_addr, 0, sizeof(ipv4_loopback_addr));
    ((char*)&ipv4_loopback_addr.sin_addr)[0] = 0x7f;
    ((char*)&ipv4_loopback_addr.sin_addr)[3] = 0x01;
    ipv4_loopback_addr.sin_family = AF_INET;
    ipv4_loopback_addr.sin_port = numeric_port;
    (*addresses_out_)
        ->emplace_back(&ipv4_loopback_addr, sizeof(ipv4_loopback_addr),
                       nullptr /* args */);
    // Let the address sorter figure out which one should be tried first.
    AddressSortingSort(this, addresses_out_->get(), "service-addresses");
    return true;
  }
  return false;
}
#else  /* GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY */
bool AresRequest::MaybeResolveLocalHostManuallyLocked() { return false; }
#endif /* GRPC_ARES_RESOLVE_LOCALHOST_MANUALLY */

std::unique_ptr<AresRequest> AresRequest::Create(
    absl::string_view dns_server, absl::string_view name,
    absl::string_view default_port, grpc_pollset_set* interested_parties,
    std::function<void(grpc_error*)> on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs,
    std::unique_ptr<grpc_core::ServerAddressList>* balancer_addrs,
    std::unique_ptr<std::string>* service_config_json, int query_timeout_ms,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer) {
  std::unique_ptr<AresRequest> r = absl::make_unique<AresRequest>(
      addrs, balancer_addrs, service_config_json,
      interested_parties, query_timeout_ms, work_serializer);
  RefCountedPtr<AresRequest::OnDoneScheduler> o(
      new AresRequest::OnDoneScheduler(r.get(), on_done));
  GRPC_CARES_TRACE_LOG(
      "request:%p c-ares grpc_dns_lookup_ares_locked_impl name=%s, "
      "default_port=%s timeout in %" PRId64 " ms",
      r.get(), std::string(name).c_str(), std::string(default_port).c_str(),
      query_timeout_ms);
  // Initialize overall DNS resolution timeout alarm
  grpc_millis timeout =
      r->query_timeout_ms_ == 0
          ? GRPC_MILLIS_INF_FUTURE
          : r->query_timeout_ms_ + grpc_core::ExecCtx::Get()->Now();
  o->WeakRef().release();  // owned by timer callback
  GRPC_CLOSURE_INIT(&r->on_timeout_locked_, AresRequest::OnTimeout, o.get(),
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&r->query_timeout_, timeout, &r->on_timeout_locked_);
  // Initialize the backup poll alarm
  grpc_millis next_ares_backup_poll_alarm =
      r->CalculateNextAresBackupPollAlarm();
  o->WeakRef().release();  // owned by timer callback
  GRPC_CLOSURE_INIT(&r->on_ares_backup_poll_alarm_locked_,
                    AresRequest::OnAresBackupPollAlarm, o.get(),
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&r->ares_backup_poll_alarm_, next_ares_backup_poll_alarm,
                  &r->on_ares_backup_poll_alarm_locked_);
  // parse name, splitting it into host and port parts
  grpc_core::SplitHostPort(name, &r->target_host_, &r->target_port_);
  if (r->target_host_.empty()) {
    r->error_ = grpc_error_set_str(
        GRPC_ERROR_CREATE_FROM_COPIED_STRING("unparseable host:port"),
        GRPC_ERROR_STR_TARGET_ADDRESS,
        grpc_slice_from_copied_string(std::string(name).c_str()));
    return r;
  } else if (r->target_port_.empty()) {
    if (default_port == nullptr) {
      r->error_ = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
          GRPC_ERROR_STR_TARGET_ADDRESS,
          grpc_slice_from_copied_string(std::string(name).c_str()));
      return r;
    }
    r->target_port_ = std::string(default_port);
  }
  // Don't query for SRV and TXT records if the target is "localhost", so
  // as to cut down on lookups over the network, especially in tests:
  // https://github.com/grpc/proposal/pull/79
  if (r->target_host_ == "localhost") {
    r->balancer_addresses_out_ = nullptr;
    r->service_config_json_out_ = nullptr;
  }
  // Early out if the target is an ipv4 or ipv6 literal.
  if (r->ResolveAsIPLiteralLocked()) {
    return r;
  }
  // Early out if the target is localhost and we're on Windows.
  if (r->MaybeResolveLocalHostManuallyLocked()) {
    return r;
  }
  // Look up name using c-ares lib.
  r->ContinueAfterCheckLocalhostAndIPLiteralsLocked(std::move(o), dns_server);
  return r;
}

void AresRequest::CancelLocked() {
  shutting_down_ = true;
  for (auto& it : fds_) {
    it.second->MaybeShutdownLocked("AresRequest::CancelLocked");
  }
}

// ares_library_init and ares_library_cleanup are currently no-op except under
// Windows. Calling them may cause race conditions when other parts of the
// binary calls these functions concurrently.
#ifdef GPR_WINDOWS
grpc_error* AresRequest::Init(void) {
  int status = ares_library_init(ARES_LIB_INIT_ALL);
  if (status != ARES_SUCCESS) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("ares_library_init failed: ", ares_strerror(status))
            .c_str());
  }
  return GRPC_ERROR_NONE;
}

void AresRequest::Shutdown(void) { ares_library_cleanup(); }
#else
grpc_error* AresRequest::Init(void) { return GRPC_ERROR_NONE; }
void AresRequest::Shutdown(void) {}
#endif  // GPR_WINDOWS

std::unique_ptr<AresRequest> (*LookupAresLocked)(
    absl::string_view dns_server, absl::string_view name,
    absl::string_view default_port, grpc_pollset_set* interested_parties,
    std::function<void(grpc_error*)> on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addrs,
    std::unique_ptr<grpc_core::ServerAddressList>* balancer_addrs,
    std::unique_ptr<std::string>* service_config_json, int query_timeout_ms,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer) =
    AresRequest::Create;

namespace {

// A GrpcResolveAddressAresRequest maintains the state need to
// carry out a single asynchronous grpc_resolve_address call.
class GrpcResolveAddressAresRequest {
 public:
  static void GrpcResolveAddressAresImpl(const char* name,
                                         const char* default_port,
                                         grpc_pollset_set* interested_parties,
                                         grpc_closure* on_done,
                                         grpc_resolved_addresses** addrs) {
    GrpcResolveAddressAresRequest* r = new GrpcResolveAddressAresRequest(
        name, default_port, interested_parties, on_done, addrs);
    auto on_resolution_done = [r](grpc_error* error) {
      r->OnDNSLookupDoneLocked(error);
    };
    r->work_serializer_->Run(
        [r, on_resolution_done]() {
          r->ares_request_ = LookupAresLocked(
              "" /* dns_server */, r->name_, r->default_port_,
              r->interested_parties_, on_resolution_done, &r->addresses_,
              nullptr /* balancer_addresses */,
              nullptr /* service_config_json */,
              GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS, r->work_serializer_);
        },
        DEBUG_LOCATION);
  }

 private:
  explicit GrpcResolveAddressAresRequest(const char* name,
                                         const char* default_port,
                                         grpc_pollset_set* interested_parties,
                                         grpc_closure* on_done,
                                         grpc_resolved_addresses** addrs_out)
      : name_(name),
        default_port_(default_port),
        interested_parties_(interested_parties),
        on_resolve_address_done_(on_done),
        addrs_out_(addrs_out) {}

  void OnDNSLookupDoneLocked(grpc_error* error) {
    grpc_resolved_addresses** resolved_addresses = addrs_out_;
    if (addresses_ == nullptr || addresses_->empty()) {
      *resolved_addresses = nullptr;
    } else {
      *resolved_addresses = static_cast<grpc_resolved_addresses*>(
          gpr_zalloc(sizeof(grpc_resolved_addresses)));
      (*resolved_addresses)->naddrs = addresses_->size();
      (*resolved_addresses)->addrs =
          static_cast<grpc_resolved_address*>(gpr_zalloc(
              sizeof(grpc_resolved_address) * (*resolved_addresses)->naddrs));
      for (size_t i = 0; i < (*resolved_addresses)->naddrs; ++i) {
        memcpy(&(*resolved_addresses)->addrs[i], &(*addresses_)[i].address(),
               sizeof(grpc_resolved_address));
      }
    }
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_resolve_address_done_, error);
    delete this;
  }

  // work_serializer that queries and related callbacks run under
  std::shared_ptr<grpc_core::WorkSerializer> work_serializer_ =
      std::make_shared<grpc_core::WorkSerializer>();
  // target name
  const char* name_;
  // default port to use if none is specified
  const char* default_port_;
  // pollset_set to be driven by
  grpc_pollset_set* interested_parties_;
  // closure to call when the resolve_address_ares request completes
  grpc_closure* on_resolve_address_done_;
  // the pointer to receive the resolved addresses
  grpc_resolved_addresses** addrs_out_;
  // currently resolving addresses
  std::unique_ptr<ServerAddressList> addresses_;
  // underlying ares_request that the query is performed on
  std::unique_ptr<AresRequest> ares_request_ = nullptr;
};

}  // namespace

void (*ResolveAddressAres)(const char* name, const char* default_port,
                           grpc_pollset_set* interested_parties,
                           grpc_closure* on_done,
                           grpc_resolved_addresses** addrs) =
    GrpcResolveAddressAresRequest::GrpcResolveAddressAresImpl;

}  // namespace grpc_core

#endif /* GRPC_ARES == 1 */
