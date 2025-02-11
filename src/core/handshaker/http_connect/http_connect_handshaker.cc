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

#include <grpc/slice.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <limits.h>
#include <string.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/core/config/core_configuration.h"
#include "src/core/handshaker/handshaker.h"
#include "src/core/handshaker/handshaker_factory.h"
#include "src/core/handshaker/handshaker_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/util/http_client/format_request.h"
#include "src/core/util/http_client/parser.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/string.h"
#include "src/core/util/sync.h"

namespace grpc_core {

namespace {

class HttpConnectHandshaker : public Handshaker {
 public:
  HttpConnectHandshaker();
  absl::string_view name() const override { return "http_connect"; }
  void DoHandshake(
      HandshakerArgs* args,
      absl::AnyInvocable<void(absl::Status)> on_handshake_done) override;
  void Shutdown(absl::Status error) override;

 private:
  ~HttpConnectHandshaker() override;
  void HandshakeFailedLocked(absl::Status error)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void FinishLocked(absl::Status error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void OnWriteDone(absl::Status error);
  void OnReadDone(absl::Status error);
  bool OnReadDoneLocked(absl::Status error) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  static void OnWriteDoneScheduler(void* arg, grpc_error_handle error);
  static void OnReadDoneScheduler(void* arg, grpc_error_handle error);

  Mutex mu_;

  // State saved while performing the handshake.
  HandshakerArgs* args_ = nullptr;
  absl::AnyInvocable<void(absl::Status)> on_handshake_done_
      ABSL_GUARDED_BY(mu_);

  // Objects for processing the HTTP CONNECT request and response.
  SliceBuffer write_buffer_ ABSL_GUARDED_BY(mu_);
  grpc_closure on_write_done_scheduler_ ABSL_GUARDED_BY(mu_);
  grpc_closure on_read_done_scheduler_ ABSL_GUARDED_BY(mu_);
  grpc_http_parser http_parser_ ABSL_GUARDED_BY(mu_);
  grpc_http_response http_response_ ABSL_GUARDED_BY(mu_);
};

HttpConnectHandshaker::~HttpConnectHandshaker() {
  grpc_http_parser_destroy(&http_parser_);
  grpc_http_response_destroy(&http_response_);
}

// If the handshake failed or we're shutting down, clean up and invoke the
// callback with the error.
void HttpConnectHandshaker::HandshakeFailedLocked(absl::Status error) {
  if (error.ok()) {
    // If we were shut down after an endpoint operation succeeded but
    // before the endpoint callback was invoked, we need to generate our
    // own error.
    error = GRPC_ERROR_CREATE("Handshaker shutdown");
  }
  absl::string_view peer_string = "[unknown]";
  if (args_ != nullptr && args_->endpoint != nullptr) {
    peer_string = grpc_endpoint_get_peer(args_->endpoint.get());
  }
  LOG_EVERY_N_SEC(ERROR, 60)
      << "HTTP proxy handshake with " << peer_string << " failed: " << error;
  // Invoke callback.
  FinishLocked(std::move(error));
}

void HttpConnectHandshaker::FinishLocked(absl::Status error) {
  InvokeOnHandshakeDone(args_, std::move(on_handshake_done_), std::move(error));
}

// This callback can be invoked inline while already holding onto the mutex. To
// avoid deadlocks, schedule OnWriteDone on ExecCtx.
// TODO(roth): This hop will no longer be needed when we migrate to the
// EventEngine endpoint API.
void HttpConnectHandshaker::OnWriteDoneScheduler(void* arg,
                                                 grpc_error_handle error) {
  auto* handshaker = static_cast<HttpConnectHandshaker*>(arg);
  handshaker->args_->event_engine->Run(
      [handshaker, error = std::move(error)]() mutable {
        ExecCtx exec_ctx;
        handshaker->OnWriteDone(std::move(error));
      });
}

// Callback invoked when finished writing HTTP CONNECT request.
void HttpConnectHandshaker::OnWriteDone(absl::Status error) {
  ReleasableMutexLock lock(&mu_);
  if (!error.ok() || args_->endpoint == nullptr) {
    // If the write failed or we're shutting down, clean up and invoke the
    // callback with the error.
    HandshakeFailedLocked(error);
    lock.Release();
    Unref();
  } else {
    // Otherwise, read the response.
    // The read callback inherits our ref to the handshaker.
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        GRPC_CLOSURE_INIT(&on_read_done_scheduler_,
                          &HttpConnectHandshaker::OnReadDoneScheduler, this,
                          grpc_schedule_on_exec_ctx),
        /*urgent=*/true, /*min_progress_size=*/1);
  }
}

// This callback can be invoked inline while already holding onto the mutex. To
// avoid deadlocks, schedule OnReadDone on ExecCtx.
// TODO(roth): This hop will no longer be needed when we migrate to the
// EventEngine endpoint API.
void HttpConnectHandshaker::OnReadDoneScheduler(void* arg,
                                                grpc_error_handle error) {
  auto* handshaker = static_cast<HttpConnectHandshaker*>(arg);
  handshaker->args_->event_engine->Run(
      [handshaker, error = std::move(error)]() mutable {
        ExecCtx exec_ctx;
        handshaker->OnReadDone(std::move(error));
      });
}

// Callback invoked for reading HTTP CONNECT response.
void HttpConnectHandshaker::OnReadDone(absl::Status error) {
  bool done;
  {
    MutexLock lock(&mu_);
    done = OnReadDoneLocked(std::move(error));
  }
  if (done) Unref();
}

bool HttpConnectHandshaker::OnReadDoneLocked(absl::Status error) {
  if (!error.ok() || args_->endpoint == nullptr) {
    // If the read failed or we're shutting down, clean up and invoke the
    // callback with the error.
    HandshakeFailedLocked(std::move(error));
    return true;
  }
  // Add buffer to parser.
  while (args_->read_buffer.Count() > 0) {
    Slice slice = args_->read_buffer.TakeFirst();
    if (!slice.empty()) {
      size_t body_start_offset = 0;
      error = grpc_http_parser_parse(&http_parser_, slice.c_slice(),
                                     &body_start_offset);
      if (!error.ok()) {
        HandshakeFailedLocked(std::move(error));
        return true;
      }
      if (http_parser_.state == GRPC_HTTP_BODY) {
        // Remove the data we've already read from the read buffer,
        // leaving only the leftover bytes (if any).
        SliceBuffer tmp_buffer;
        if (body_start_offset < slice.length()) {
          tmp_buffer.Append(slice.Split(body_start_offset));
        }
        tmp_buffer.TakeAndAppend(args_->read_buffer);
        tmp_buffer.Swap(&args_->read_buffer);
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
  if (http_parser_.state != GRPC_HTTP_BODY) {
    args_->read_buffer.Clear();
    grpc_endpoint_read(
        args_->endpoint.get(), args_->read_buffer.c_slice_buffer(),
        GRPC_CLOSURE_INIT(&on_read_done_scheduler_,
                          &HttpConnectHandshaker::OnReadDoneScheduler, this,
                          grpc_schedule_on_exec_ctx),
        /*urgent=*/true, /*min_progress_size=*/1);
    return false;
  }
  // Make sure we got a 2xx response.
  if (http_response_.status < 200 || http_response_.status >= 300) {
    error = GRPC_ERROR_CREATE(absl::StrCat("HTTP proxy returned response code ",
                                           http_response_.status));
    HandshakeFailedLocked(std::move(error));
    return true;
  }
  // Success.  Invoke handshake-done callback.
  FinishLocked(absl::OkStatus());
  return true;
}

//
// Public handshaker methods
//

void HttpConnectHandshaker::Shutdown(absl::Status /*error*/) {
  MutexLock lock(&mu_);
  if (on_handshake_done_ != nullptr) args_->endpoint.reset();
}

void HttpConnectHandshaker::DoHandshake(
    HandshakerArgs* args,
    absl::AnyInvocable<void(absl::Status)> on_handshake_done) {
  // Check for HTTP CONNECT channel arg.
  // If not found, invoke on_handshake_done without doing anything.
  std::optional<absl::string_view> server_name =
      args->args.GetString(GRPC_ARG_HTTP_CONNECT_SERVER);
  if (!server_name.has_value()) {
    InvokeOnHandshakeDone(args, std::move(on_handshake_done), absl::OkStatus());
    return;
  }
  // Get headers from channel args.
  std::optional<absl::string_view> arg_header_string =
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
        LOG(ERROR) << "skipping unparsable HTTP CONNECT header: "
                   << header_strings[i];
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
  on_handshake_done_ = std::move(on_handshake_done);
  // Log connection via proxy.
  std::string proxy_name(grpc_endpoint_get_peer(args->endpoint.get()));
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
  write_buffer_.Append(Slice(request_slice));
  // Clean up.
  gpr_free(headers);
  for (size_t i = 0; i < num_header_strings; ++i) {
    gpr_free(header_strings[i]);
  }
  gpr_free(header_strings);
  // Take a new ref to be held by the write callback.
  Ref().release();
  grpc_endpoint_write(
      args->endpoint.get(), write_buffer_.c_slice_buffer(),
      GRPC_CLOSURE_INIT(&on_write_done_scheduler_,
                        &HttpConnectHandshaker::OnWriteDoneScheduler, this,
                        grpc_schedule_on_exec_ctx),
      nullptr, /*max_frame_size=*/INT_MAX);
}

HttpConnectHandshaker::HttpConnectHandshaker() {
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
