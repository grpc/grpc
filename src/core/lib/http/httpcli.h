/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_HTTP_HTTPCLI_H
#define GRPC_CORE_LIB_HTTP_HTTPCLI_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include <grpc/support/time.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/resource_quota/resource_quota.h"

/* User agent this library reports */
#define GRPC_HTTPCLI_USER_AGENT "grpc-httpcli/0.0"

/* TODO(ctiller): allow caching and capturing multiple requests for the
                  same content and combining them */
struct grpc_httpcli_handshaker {
  const char* default_port;
  void (*handshake)(void* arg, grpc_endpoint* endpoint, const char* host,
                    grpc_millis deadline,
                    void (*on_done)(void* arg, grpc_endpoint* endpoint));
};
extern const grpc_httpcli_handshaker grpc_httpcli_plaintext;
extern const grpc_httpcli_handshaker grpc_httpcli_ssl;

/* A request */
typedef struct grpc_httpcli_request {
  /* The host name to connect to */
  char* host;
  /* The host to verify in the SSL handshake (or NULL) */
  char* ssl_host_override;
  /* The main part of the request
     The following headers are supplied automatically and MUST NOT be set here:
     Host, Connection, User-Agent */
  grpc_http_request http;
  /* handshaker to use ssl for the request */
  const grpc_httpcli_handshaker* handshaker;
} grpc_httpcli_request;

/* Expose the parser response type as a httpcli response too */
typedef struct grpc_http_response grpc_httpcli_response;

/* override functions return 1 if they handled the request, 0 otherwise */
typedef int (*grpc_httpcli_get_override)(const grpc_httpcli_request* request,
                                         grpc_millis deadline,
                                         grpc_closure* on_complete,
                                         grpc_httpcli_response* response);
typedef int (*grpc_httpcli_post_override)(const grpc_httpcli_request* request,
                                          const char* body_bytes,
                                          size_t body_size,
                                          grpc_millis deadline,
                                          grpc_closure* on_complete,
                                          grpc_httpcli_response* response);

namespace grpc_core {

// Tracks an in-progress GET or POST request. Calling \a Start()
// begins async work and calling \a Orphan() arranges for aysnc work
// to be completed as sooon as possible (possibly aborting the request
// if it's in flight).
class HttpCliRequest : public InternallyRefCounted<HttpCliRequest> {
 public:
  // Asynchronously perform a HTTP GET.
  // 'pollent' indicates a grpc_polling_entity that is interested in the result
  //   of the get - work on this entity may be used to progress the get
  //   operation
  // 'resource_quota' allows the caller to specify the quota against which to
  // allocate
  // 'request' contains request parameters - these are caller owned and
  // can be destroyed once the call returns 'deadline' contains a deadline for
  // the request (or gpr_inf_future) 'on_response' is a callback to report
  // results to
  static OrphanablePtr<HttpCliRequest> Get(
      grpc_polling_entity* pollent, ResourceQuotaRefPtr resource_quota,
      const grpc_httpcli_request* request, grpc_millis deadline,
      grpc_closure* on_done,
      grpc_httpcli_response* response) GRPC_MUST_USE_RESULT;

  // Asynchronously perform a HTTP POST.
  // 'pollent' indicates a grpc_polling_entity that is interested in the result
  //   of the post - work on this entity may be used to progress the post
  //   operation
  // 'resource_quota' allows the caller to specify the quota against which to
  // allocate
  // 'request' contains request parameters - these are caller owned and can be
  //   destroyed once the call returns
  // 'body_bytes' and 'body_size' specify the payload for the post.
  //   When there is no body, pass in NULL as body_bytes.
  // 'deadline' contains a deadline for the request (or gpr_inf_future)
  // 'em' points to a caller owned event manager that must be alive for the
  //   lifetime of the request
  // 'on_response' is a callback to report results to
  // Does not support ?var1=val1&var2=val2 in the path.
  static OrphanablePtr<HttpCliRequest> Post(
      grpc_polling_entity* pollent, ResourceQuotaRefPtr resource_quota,
      const grpc_httpcli_request* request, const char* body_bytes,
      size_t body_size, grpc_millis deadline, grpc_closure* on_done,
      grpc_httpcli_response* response) GRPC_MUST_USE_RESULT;

  void SetOverride(grpc_httpcli_get_override get,
                   grpc_httpcli_post_override post);

  HttpCliRequest(const grpc_slice& request_text,
                 grpc_httpcli_response* response,
                 ResourceQuotaRefPtr resource_quota, absl::string_view host,
                 absl::string_view ssl_host_override, grpc_millis deadline,
                 const grpc_httpcli_handshaker* handshaker,
                 grpc_closure* on_done, grpc_polling_entity* pollent,
                 const char* name);

  ~HttpCliRequest();

  void Start();

  void Orphan() override {
    //grpc_core::MutexLock lock(&mu_);
    // TODO(apolcyn): implement cancellation
    Unref();
  }

 private:
  void Finish(grpc_error_handle error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    ExecCtx::Run(DEBUG_LOCATION, on_done_, error);
    Unref();
  }

  void AppendError(grpc_error_handle error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void DoRead() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    grpc_endpoint_read(ep_, &incoming_, &on_read_, /*urgent=*/true);
  }

  static void OnRead(void* user_data, grpc_error_handle error) {
    HttpCliRequest* req = static_cast<HttpCliRequest*>(user_data);
    ExecCtx::Run(DEBUG_LOCATION, &req->continue_on_read_after_schedule_on_exec_ctx_, GRPC_ERROR_REF(error));
  }

  // Needed since OnRead may be called inline from grpc_endpoint_read
  static void ContinueOnReadAfterScheduleOnExecCtx(void* user_data, grpc_error_handle error) {
    HttpCliRequest* req = static_cast<HttpCliRequest*>(user_data);
    grpc_core::MutexLock lock(&req->mu_);
    req->OnReadInternal(error);
  }

  void OnReadInternal(grpc_error_handle error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void OnWritten() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { DoRead(); }

  static void DoneWrite(void* arg, grpc_error_handle error) {
    HttpCliRequest* req = static_cast<HttpCliRequest*>(arg);
    ExecCtx::Run(DEBUG_LOCATION, &req->continue_done_write_after_schedule_on_exec_ctx_, GRPC_ERROR_REF(error));
  }

  // Needed since DoneWrite may be called inline from grpc_endpoint_write
  static void ContinueDoneWriteAfterScheduleOnExecCtx(void* arg, grpc_error_handle error);

  void StartWrite() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  static void OnHandshakeDone(void* arg, grpc_endpoint* ep);

  static void OnConnected(void* arg, grpc_error_handle error);

  void NextAddress(grpc_error_handle error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void OnResolved(
      absl::StatusOr<std::vector<grpc_resolved_address>> addresses_or);

  const grpc_httpcli_handshaker* handshaker_;
  grpc_closure on_read_;
  grpc_closure continue_on_read_after_schedule_on_exec_ctx_;
  grpc_closure done_write_;
  grpc_closure continue_done_write_after_schedule_on_exec_ctx_;
  grpc_closure connected_;
  grpc_core::Mutex mu_;
  grpc_slice request_text_ ABSL_GUARDED_BY(mu_);
  grpc_http_parser parser_ ABSL_GUARDED_BY(mu_);
  std::vector<grpc_resolved_address> addresses_ ABSL_GUARDED_BY(mu_);
  size_t next_address_ ABSL_GUARDED_BY(mu_) = 0;
  grpc_endpoint* ep_ ABSL_GUARDED_BY(mu_) = nullptr;
  ResourceQuotaRefPtr resource_quota_ ABSL_GUARDED_BY(mu_);
  std::string host_ ABSL_GUARDED_BY(mu_);
  std::string ssl_host_override_ ABSL_GUARDED_BY(mu_);
  grpc_millis deadline_ ABSL_GUARDED_BY(mu_);
  int have_read_byte_ ABSL_GUARDED_BY(mu_) = 0;
  grpc_closure* on_done_ ABSL_GUARDED_BY(mu_);
  grpc_pollset_set* pollset_set_ ABSL_GUARDED_BY(mu_);
  grpc_iomgr_object iomgr_obj_ ABSL_GUARDED_BY(mu_);
  grpc_slice_buffer incoming_ ABSL_GUARDED_BY(mu_);
  grpc_slice_buffer outgoing_ ABSL_GUARDED_BY(mu_);
  grpc_error_handle overall_error_ ABSL_GUARDED_BY(mu_) = GRPC_ERROR_NONE;
  OrphanablePtr<DNSResolver::Request> dns_request_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_HTTP_HTTPCLI_H */
