//
//
// Copyright 2016 gRPC authors.
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
//

#include "src/core/handshaker/http_connect/http_connect_handshaker.h"

#include <limits.h>
#include <string.h>

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_factory.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/debug_location.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/util/http_client/format_request.h"
#include "src/core/util/http_client/parser.h"
#include "src/core/util/string.h"

namespace grpc_core {

namespace {

class HttpConnectHandshaker : public Handshaker {
 public:
  HttpConnectHandshaker();
  void Shutdown(grpc_error_handle why) override;
  void DoHandshake(grpc_tcp_server_acceptor* acceptor,
                   grpc_closure* on_handshake_done,
                   HandshakerArgs* args) override;
  const char* name() const override { return "http_connect"; }

 private:
  ~HttpConnectHandshaker() override;
  void CleanupArgsForFailureLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void HandshakeFailedLocked(grpc_error_handle error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void OnWriteDone(void* arg, grpc_error_handle error);
  static void OnReadDone(void* arg, grpc_error_handle error);
  static void OnWriteDoneScheduler(void* arg, grpc_error_handle error);
  static void OnReadDoneScheduler(void* arg, grpc_error_handle error);

  Mutex mu_;

  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
  // Read buffer to destroy after a shutdown.
  grpc_slice_buffer* read_buffer_to_destroy_ ABSL_GUARDED_BY(mu_) = nullptr;

  // State saved while performing the handshake.
  HandshakerArgs* args_ = nullptr;
  grpc_closure* on_handshake_done_ = nullptr;

  // Objects for processing the HTTP CONNECT request and response.
  grpc_slice_buffer write_buffer_ ABSL_GUARDED_BY(mu_);
  grpc_closure request_done_closure_ ABSL_GUARDED_BY(mu_);
  grpc_closure response_read_closure_ ABSL_GUARDED_BY(mu_);
  grpc_http_parser http_parser_ ABSL_GUARDED_BY(mu_);
  grpc_http_response http_response_ ABSL_GUARDED_BY(mu_);
};

HttpConnectHandshaker::~HttpConnectHandshaker() {
  if (read_buffer_to_destroy_ != nullptr) {
    grpc_slice_buffer_destroy(read_buffer_to_destroy_);
    gpr_free(read_buffer_to_destroy_);
  }
  grpc_slice_buffer_destroy(&write_buffer_);
  grpc_http_parser_destroy(&http_parser_);
  grpc_http_response_destroy(&http_response_);
}

// Set args fields to nullptr, saving the endpoint and read buffer for
// later destruction.
void HttpConnectHandshaker::CleanupArgsForFailureLocked() {
  read_buffer_to_destroy_ = args_->read_buffer;
  args_->read_buffer = nullptr;
  args_->args = ChannelArgs();
}

// If the handshake failed or we're shutting down, clean up and invoke the
// callback with the error.
void HttpConnectHandshaker::HandshakeFailedLocked(grpc_error_handle error) {
  if (error.ok()) {
    // If we were shut down after an endpoint operation succeeded but
    // before the endpoint callback was invoked, we need to generate our
    // own error.
    error = GRPC_ERROR_CREATE("Handshaker shutdown");
  }
  if (!is_shutdown_) {
    // Not shutting down, so the handshake failed.  Clean up before
    // invoking the callback.
    grpc_endpoint_destroy(args_->endpoint);
    args_->endpoint = nullptr;
    CleanupArgsForFailureLocked();
    // Set shutdown to true so that subsequent calls to
    // http_connect_handshaker_shutdown() do nothing.
    is_shutdown_ = true;
  }
  // Invoke callback.
  ExecCtx::Run(DEBUG_LOCATION, on_handshake_done_, error);
}

// This callback can be invoked inline while already holding onto the mutex. To
// avoid deadlocks, schedule OnWriteDone on ExecCtx.
void HttpConnectHandshaker::OnWriteDoneScheduler(void* arg,
                                                 grpc_error_handle error) {
  auto* handshaker = static_cast<HttpConnectHandshaker*>(arg);
  ExecCtx::Run(DEBUG_LOCATION,
               GRPC_CLOSURE_INIT(&handshaker->request_done_closure_,
                                 &HttpConnectHandshaker::OnWriteDone,
                                 handshaker, grpc_schedule_on_exec_ctx),
               error);
}

// Callback invoked when finished writing HTTP CONNECT request.
void HttpConnectHandshaker::OnWriteDone(void* arg, grpc_error_handle error) {
  auto* handshaker = static_cast<HttpConnectHandshaker*>(arg);
  ReleasableMutexLock lock(&handshaker->mu_);
  if (!error.ok() || handshaker->is_shutdown_) {
    // If the write failed or we're shutting down, clean up and invoke the
    // callback with the error.
    handshaker->HandshakeFailedLocked(error);
    lock.Release();
    handshaker->Unref();
  } else {
    // Otherwise, read the response.
    // The read callback inherits our ref to the handshaker.
    grpc_endpoint_read(
        handshaker->args_->endpoint, handshaker->args_->read_buffer,
        GRPC_CLOSURE_INIT(&handshaker->response_read_closure_,
                          &HttpConnectHandshaker::OnReadDoneScheduler,
                          handshaker, grpc_schedule_on_exec_ctx),
        /*urgent=*/true, /*min_progress_size=*/1);
  }
}

// This callback can be invoked inline while already holding onto the mutex. To
// avoid deadlocks, schedule OnReadDone on ExecCtx.
void HttpConnectHandshaker::OnReadDoneScheduler(void* arg,
                                                grpc_error_handle error) {
  auto* handshaker = static_cast<HttpConnectHandshaker*>(arg);
  ExecCtx::Run(DEBUG_LOCATION,
               GRPC_CLOSURE_INIT(&handshaker->response_read_closure_,
                                 &HttpConnectHandshaker::OnReadDone, handshaker,
                                 grpc_schedule_on_exec_ctx),
               error);
}

// Callback invoked for reading HTTP CONNECT response.
void HttpConnectHandshaker::OnReadDone(void* arg, grpc_error_handle error) {
  auto* handshaker = static_cast<HttpConnectHandshaker*>(arg);
  ReleasableMutexLock lock(&handshaker->mu_);
  if (!error.ok() || handshaker->is_shutdown_) {
    // If the read failed or we're shutting down, clean up and invoke the
    // callback with the error.
    handshaker->HandshakeFailedLocked(error);
    goto done;
  }
  // Add buffer to parser.
  for (size_t i = 0; i < handshaker->args_->read_buffer->count; ++i) {
    if (GRPC_SLICE_LENGTH(handshaker->args_->read_buffer->slices[i]) > 0) {
      size_t body_start_offset = 0;
      error = grpc_http_parser_parse(&handshaker->http_parser_,
                                     handshaker->args_->read_buffer->slices[i],
                                     &body_start_offset);
      if (!error.ok()) {
        handshaker->HandshakeFailedLocked(error);
        goto done;
      }
      if (handshaker->http_parser_.state == GRPC_HTTP_BODY) {
        // Remove the data we've already read from the read buffer,
        // leaving only the leftover bytes (if any).
        grpc_slice_buffer tmp_buffer;
        grpc_slice_buffer_init(&tmp_buffer);
        if (body_start_offset <
            GRPC_SLICE_LENGTH(handshaker->args_->read_buffer->slices[i])) {
          grpc_slice_buffer_add(
              &tmp_buffer,
              grpc_slice_split_tail(&handshaker->args_->read_buffer->slices[i],
                                    body_start_offset));
        }
        grpc_slice_buffer_addn(&tmp_buffer,
                               &handshaker->args_->read_buffer->slices[i + 1],
                               handshaker->args_->read_buffer->count - i - 1);
        grpc_slice_buffer_swap(handshaker->args_->read_buffer, &tmp_buffer);
        grpc_slice_buffer_destroy(&tmp_buffer);
        break;
      }
    }
  }
  // If we're not done reading the response, read more data.
  // TODO(roth): In practice, I suspect that the response to a CONNECT
  // request will never include a body, in which case this check is
  // sufficient.  However, the language of RFC-2817 doesn't explicitly
  // forbid the response from including a body.  If there is a body,
  // it's possible that we might have parsed part but not all of the
  // body, in which case this check will cause us to fail to parse the
  // remainder of the body.  If that ever becomes an issue, we may
  // need to fix the HTTP parser to understand when the body is
  // complete (e.g., handling chunked transfer encoding or looking
  // at the Content-Length: header).
  if (handshaker->http_parser_.state != GRPC_HTTP_BODY) {
    grpc_slice_buffer_reset_and_unref(handshaker->args_->read_buffer);
    grpc_endpoint_read(
        handshaker->args_->endpoint, handshaker->args_->read_buffer,
        GRPC_CLOSURE_INIT(&handshaker->response_read_closure_,
                          &HttpConnectHandshaker::OnReadDoneScheduler,
                          handshaker, grpc_schedule_on_exec_ctx),
        /*urgent=*/true, /*min_progress_size=*/1);
    return;
  }
  // Make sure we got a 2xx response.
  if (handshaker->http_response_.status < 200 ||
      handshaker->http_response_.status >= 300) {
    error = GRPC_ERROR_CREATE(absl::StrCat("HTTP proxy returned response code ",
                                           handshaker->http_response_.status));
    handshaker->HandshakeFailedLocked(error);
    goto done;
  }
  // Success.  Invoke handshake-done callback.
  ExecCtx::Run(DEBUG_LOCATION, handshaker->on_handshake_done_, error);
done:
  // Set shutdown to true so that subsequent calls to
  // http_connect_handshaker_shutdown() do nothing.
  handshaker->is_shutdown_ = true;
  lock.Release();
  handshaker->Unref();
}

//
// Public handshaker methods
//

void HttpConnectHandshaker::Shutdown(grpc_error_handle /*why*/) {
  {
    MutexLock lock(&mu_);
    if (!is_shutdown_) {
      is_shutdown_ = true;
      grpc_endpoint_destroy(args_->endpoint);
      args_->endpoint = nullptr;
      CleanupArgsForFailureLocked();
    }
  }
}

void HttpConnectHandshaker::DoHandshake(grpc_tcp_server_acceptor* /*acceptor*/,
                                        grpc_closure* on_handshake_done,
                                        HandshakerArgs* args) {
  // Check for HTTP CONNECT channel arg.
  // If not found, invoke on_handshake_done without doing anything.
  absl::optional<absl::string_view> server_name =
      args->args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER);
  if (!server_name.has_value()) {
    // Set shutdown to true so that subsequent calls to
    // http_connect_handshaker_shutdown() do nothing.
    {
      MutexLock lock(&mu_);
      is_shutdown_ = true;
    }
    ExecCtx::Run(DEBUG_LOCATION, on_handshake_done, absl::OkStatus());
    return;
  }
  // Get headers from channel args.
  absl::optional<absl::string_view> arg_header_string =
      args->args.GetString(GRPC_ARG_HTTP_CONNECT_HEADERS);
  grpc_http_header* headers = nullptr;
  size_t num_headers = 0;
  char** header_strings = nullptr;
  size_t num_header_strings = 0;
  if (arg_header_string.has_value()) {
    std::string buffer(*arg_header_string);
    gpr_string_split(buffer.c_str(), "\n", &header_strings,
                     &num_header_strings);
    headers = static_cast<grpc_http_header*>(
        gpr_malloc(sizeof(grpc_http_header) * num_header_strings));
    for (size_t i = 0; i < num_header_strings; ++i) {
      char* sep = strchr(header_strings[i], ':');

      if (sep == nullptr) {
        gpr_log(GPR_ERROR, "skipping unparseable HTTP CONNECT header: %s",
                header_strings[i]);
        continue;
      }
      *sep = '\0';
      headers[num_headers].key = header_strings[i];
      headers[num_headers].value = sep + 1;
      ++num_headers;
    }
  }
  // Save state in the handshaker object.
  MutexLock lock(&mu_);
  args_ = args;
  on_handshake_done_ = on_handshake_done;
  // Log connection via proxy.
  std::string proxy_name(grpc_endpoint_get_peer(args->endpoint));
  std::string server_name_string(*server_name);
  VLOG(2) << "Connecting to server " << server_name_string << " via HTTP proxy "
          << proxy_name;
  // Construct HTTP CONNECT request.
  grpc_http_request request;
  request.method = const_cast<char*>("CONNECT");
  request.version = GRPC_HTTP_HTTP10;  // Set by OnReadDone
  request.hdrs = headers;
  request.hdr_count = num_headers;
  request.body_length = 0;
  request.body = nullptr;
  grpc_slice request_slice = grpc_httpcli_format_connect_request(
      &request, server_name_string.c_str(), server_name_string.c_str());
  grpc_slice_buffer_add(&write_buffer_, request_slice);
  // Clean up.
  gpr_free(headers);
  for (size_t i = 0; i < num_header_strings; ++i) {
    gpr_free(header_strings[i]);
  }
  gpr_free(header_strings);
  // Take a new ref to be held by the write callback.
  Ref().release();
  grpc_endpoint_write(
      args->endpoint, &write_buffer_,
      GRPC_CLOSURE_INIT(&request_done_closure_,
                        &HttpConnectHandshaker::OnWriteDoneScheduler, this,
                        grpc_schedule_on_exec_ctx),
      nullptr, /*max_frame_size=*/INT_MAX);
}

HttpConnectHandshaker::HttpConnectHandshaker() {
  grpc_slice_buffer_init(&write_buffer_);
  grpc_http_parser_init(&http_parser_, GRPC_HTTP_RESPONSE, &http_response_);
}

//
// handshaker factory
//

class HttpConnectHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const ChannelArgs& /*args*/,
                      grpc_pollset_set* /*interested_parties*/,
                      HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(MakeRefCounted<HttpConnectHandshaker>());
  }
  HandshakerPriority Priority() override {
    return HandshakerPriority::kHTTPConnectHandshakers;
  }
  ~HttpConnectHandshakerFactory() override = default;
};

}  // namespace

void RegisterHttpConnectHandshaker(CoreConfiguration::Builder* builder) {
  builder->handshaker_registry()->RegisterHandshakerFactory(
      HANDSHAKER_CLIENT, std::make_unique<HttpConnectHandshakerFactory>());
}

}  // namespace grpc_core
