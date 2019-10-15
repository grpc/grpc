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

#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"

#include <string.h>

#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/handshaker_registry.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/http/format_request.h"
#include "src/core/lib/http/parser.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

namespace {

class HttpConnectHandshaker : public Handshaker {
 public:
  HttpConnectHandshaker();
  void Shutdown(grpc_error* why) override;
  void DoHandshake(grpc_tcp_server_acceptor* acceptor,
                   grpc_closure* on_handshake_done,
                   HandshakerArgs* args) override;
  const char* name() const override { return "http_connect"; }

 private:
  virtual ~HttpConnectHandshaker();
  void CleanupArgsForFailureLocked();
  void HandshakeFailedLocked(grpc_error* error);
  static void OnWriteDone(void* arg, grpc_error* error);
  static void OnReadDone(void* arg, grpc_error* error);

  gpr_mu mu_;

  bool is_shutdown_ = false;
  // Endpoint and read buffer to destroy after a shutdown.
  grpc_endpoint* endpoint_to_destroy_ = nullptr;
  grpc_slice_buffer* read_buffer_to_destroy_ = nullptr;

  // State saved while performing the handshake.
  HandshakerArgs* args_ = nullptr;
  grpc_closure* on_handshake_done_ = nullptr;

  // Objects for processing the HTTP CONNECT request and response.
  grpc_slice_buffer write_buffer_;
  grpc_closure request_done_closure_;
  grpc_closure response_read_closure_;
  grpc_http_parser http_parser_;
  grpc_http_response http_response_;
};

HttpConnectHandshaker::~HttpConnectHandshaker() {
  gpr_mu_destroy(&mu_);
  if (endpoint_to_destroy_ != nullptr) {
    grpc_endpoint_destroy(endpoint_to_destroy_);
  }
  if (read_buffer_to_destroy_ != nullptr) {
    grpc_slice_buffer_destroy_internal(read_buffer_to_destroy_);
    gpr_free(read_buffer_to_destroy_);
  }
  grpc_slice_buffer_destroy_internal(&write_buffer_);
  grpc_http_parser_destroy(&http_parser_);
  grpc_http_response_destroy(&http_response_);
}

// Set args fields to nullptr, saving the endpoint and read buffer for
// later destruction.
void HttpConnectHandshaker::CleanupArgsForFailureLocked() {
  endpoint_to_destroy_ = args_->endpoint;
  args_->endpoint = nullptr;
  read_buffer_to_destroy_ = args_->read_buffer;
  args_->read_buffer = nullptr;
  grpc_channel_args_destroy(args_->args);
  args_->args = nullptr;
}

// If the handshake failed or we're shutting down, clean up and invoke the
// callback with the error.
void HttpConnectHandshaker::HandshakeFailedLocked(grpc_error* error) {
  if (error == GRPC_ERROR_NONE) {
    // If we were shut down after an endpoint operation succeeded but
    // before the endpoint callback was invoked, we need to generate our
    // own error.
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshaker shutdown");
  }
  if (!is_shutdown_) {
    // TODO(ctiller): It is currently necessary to shutdown endpoints
    // before destroying them, even if we know that there are no
    // pending read/write callbacks.  This should be fixed, at which
    // point this can be removed.
    grpc_endpoint_shutdown(args_->endpoint, GRPC_ERROR_REF(error));
    // Not shutting down, so the handshake failed.  Clean up before
    // invoking the callback.
    CleanupArgsForFailureLocked();
    // Set shutdown to true so that subsequent calls to
    // http_connect_handshaker_shutdown() do nothing.
    is_shutdown_ = true;
  }
  // Invoke callback.
  GRPC_CLOSURE_SCHED(on_handshake_done_, error);
}

// Callback invoked when finished writing HTTP CONNECT request.
void HttpConnectHandshaker::OnWriteDone(void* arg, grpc_error* error) {
  auto* handshaker = static_cast<HttpConnectHandshaker*>(arg);
  gpr_mu_lock(&handshaker->mu_);
  if (error != GRPC_ERROR_NONE || handshaker->is_shutdown_) {
    // If the write failed or we're shutting down, clean up and invoke the
    // callback with the error.
    handshaker->HandshakeFailedLocked(GRPC_ERROR_REF(error));
    gpr_mu_unlock(&handshaker->mu_);
    handshaker->Unref();
  } else {
    // Otherwise, read the response.
    // The read callback inherits our ref to the handshaker.
    grpc_endpoint_read(handshaker->args_->endpoint,
                       handshaker->args_->read_buffer,
                       &handshaker->response_read_closure_, /*urgent=*/true);
    gpr_mu_unlock(&handshaker->mu_);
  }
}

// Callback invoked for reading HTTP CONNECT response.
void HttpConnectHandshaker::OnReadDone(void* arg, grpc_error* error) {
  auto* handshaker = static_cast<HttpConnectHandshaker*>(arg);

  gpr_mu_lock(&handshaker->mu_);
  if (error != GRPC_ERROR_NONE || handshaker->is_shutdown_) {
    // If the read failed or we're shutting down, clean up and invoke the
    // callback with the error.
    handshaker->HandshakeFailedLocked(GRPC_ERROR_REF(error));
    goto done;
  }
  // Add buffer to parser.
  for (size_t i = 0; i < handshaker->args_->read_buffer->count; ++i) {
    if (GRPC_SLICE_LENGTH(handshaker->args_->read_buffer->slices[i]) > 0) {
      size_t body_start_offset = 0;
      error = grpc_http_parser_parse(&handshaker->http_parser_,
                                     handshaker->args_->read_buffer->slices[i],
                                     &body_start_offset);
      if (error != GRPC_ERROR_NONE) {
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
        grpc_slice_buffer_destroy_internal(&tmp_buffer);
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
    grpc_slice_buffer_reset_and_unref_internal(handshaker->args_->read_buffer);
    grpc_endpoint_read(handshaker->args_->endpoint,
                       handshaker->args_->read_buffer,
                       &handshaker->response_read_closure_, /*urgent=*/true);
    gpr_mu_unlock(&handshaker->mu_);
    return;
  }
  // Make sure we got a 2xx response.
  if (handshaker->http_response_.status < 200 ||
      handshaker->http_response_.status >= 300) {
    char* msg;
    gpr_asprintf(&msg, "HTTP proxy returned response code %d",
                 handshaker->http_response_.status);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    handshaker->HandshakeFailedLocked(error);
    goto done;
  }
  // Success.  Invoke handshake-done callback.
  GRPC_CLOSURE_SCHED(handshaker->on_handshake_done_, error);
done:
  // Set shutdown to true so that subsequent calls to
  // http_connect_handshaker_shutdown() do nothing.
  handshaker->is_shutdown_ = true;
  gpr_mu_unlock(&handshaker->mu_);
  handshaker->Unref();
}

//
// Public handshaker methods
//

void HttpConnectHandshaker::Shutdown(grpc_error* why) {
  gpr_mu_lock(&mu_);
  if (!is_shutdown_) {
    is_shutdown_ = true;
    grpc_endpoint_shutdown(args_->endpoint, GRPC_ERROR_REF(why));
    CleanupArgsForFailureLocked();
  }
  gpr_mu_unlock(&mu_);
  GRPC_ERROR_UNREF(why);
}

void HttpConnectHandshaker::DoHandshake(grpc_tcp_server_acceptor* acceptor,
                                        grpc_closure* on_handshake_done,
                                        HandshakerArgs* args) {
  // Check for HTTP CONNECT channel arg.
  // If not found, invoke on_handshake_done without doing anything.
  const grpc_arg* arg =
      grpc_channel_args_find(args->args, GRPC_ARG_HTTP_CONNECT_SERVER);
  char* server_name = grpc_channel_arg_get_string(arg);
  if (server_name == nullptr) {
    // Set shutdown to true so that subsequent calls to
    // http_connect_handshaker_shutdown() do nothing.
    gpr_mu_lock(&mu_);
    is_shutdown_ = true;
    gpr_mu_unlock(&mu_);
    GRPC_CLOSURE_SCHED(on_handshake_done, GRPC_ERROR_NONE);
    return;
  }
  // Get headers from channel args.
  arg = grpc_channel_args_find(args->args, GRPC_ARG_HTTP_CONNECT_HEADERS);
  char* arg_header_string = grpc_channel_arg_get_string(arg);
  grpc_http_header* headers = nullptr;
  size_t num_headers = 0;
  char** header_strings = nullptr;
  size_t num_header_strings = 0;
  if (arg_header_string != nullptr) {
    gpr_string_split(arg_header_string, "\n", &header_strings,
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
  char* proxy_name = grpc_endpoint_get_peer(args->endpoint);
  gpr_log(GPR_INFO, "Connecting to server %s via HTTP proxy %s", server_name,
          proxy_name);
  gpr_free(proxy_name);
  // Construct HTTP CONNECT request.
  grpc_httpcli_request request;
  request.host = server_name;
  request.ssl_host_override = nullptr;
  request.http.method = (char*)"CONNECT";
  request.http.path = server_name;
  request.http.version = GRPC_HTTP_HTTP10;  // Set by OnReadDone
  request.http.hdrs = headers;
  request.http.hdr_count = num_headers;
  request.http.body_length = 0;
  request.http.body = nullptr;
  request.handshaker = &grpc_httpcli_plaintext;
  grpc_slice request_slice = grpc_httpcli_format_connect_request(&request);
  grpc_slice_buffer_add(&write_buffer_, request_slice);
  // Clean up.
  gpr_free(headers);
  for (size_t i = 0; i < num_header_strings; ++i) {
    gpr_free(header_strings[i]);
  }
  gpr_free(header_strings);
  // Take a new ref to be held by the write callback.
  Ref().release();
  grpc_endpoint_write(args->endpoint, &write_buffer_, &request_done_closure_,
                      nullptr);
}

HttpConnectHandshaker::HttpConnectHandshaker() {
  gpr_mu_init(&mu_);
  grpc_slice_buffer_init(&write_buffer_);
  GRPC_CLOSURE_INIT(&request_done_closure_, &HttpConnectHandshaker::OnWriteDone,
                    this, grpc_schedule_on_exec_ctx);
  GRPC_CLOSURE_INIT(&response_read_closure_, &HttpConnectHandshaker::OnReadDone,
                    this, grpc_schedule_on_exec_ctx);
  grpc_http_parser_init(&http_parser_, GRPC_HTTP_RESPONSE, &http_response_);
}

//
// handshaker factory
//

class HttpConnectHandshakerFactory : public HandshakerFactory {
 public:
  void AddHandshakers(const grpc_channel_args* args,
                      grpc_pollset_set* interested_parties,
                      HandshakeManager* handshake_mgr) override {
    handshake_mgr->Add(MakeRefCounted<HttpConnectHandshaker>());
  }
  ~HttpConnectHandshakerFactory() override = default;
};

}  // namespace

}  // namespace grpc_core

void grpc_http_connect_register_handshaker_factory() {
  using namespace grpc_core;
  HandshakerRegistry::RegisterHandshakerFactory(
      true /* at_start */, HANDSHAKER_CLIENT,
      MakeUnique<HttpConnectHandshakerFactory>());
}
