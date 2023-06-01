//
//
// Copyright 2018 gRPC authors.
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
#include "test/core/tsi/alts/fake_handshaker/fake_handshaker_server.h"

#include <memory>
#include <sstream>
#include <string>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpcpp/impl/sync.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/async_stream.h>

#include "src/core/lib/gprpp/crash.h"
#include "test/core/tsi/alts/fake_handshaker/handshaker.grpc.pb.h"
#include "test/core/tsi/alts/fake_handshaker/handshaker.pb.h"
#include "test/core/tsi/alts/fake_handshaker/transport_security_common.pb.h"

// Fake handshake messages.
constexpr char kClientInitFrame[] = "ClientInit";
constexpr char kServerFrame[] = "ServerInitAndFinished";
constexpr char kClientFinishFrame[] = "ClientFinished";
// Error messages.
constexpr char kInvalidFrameError[] = "Invalid input frame.";
constexpr char kWrongStateError[] = "Wrong handshake state.";

namespace grpc {
namespace gcp {

// FakeHandshakeService implements a fake handshaker service using a fake key
// exchange protocol. The fake key exchange protocol is a 3-message protocol:
// - Client first sends ClientInit message to Server.
// - Server then sends ServerInitAndFinished message back to Client.
// - Client finally sends ClientFinished message to Server.
// This fake handshaker service is intended for ALTS integration testing without
// relying on real ALTS handshaker service inside GCE.
// It is thread-safe.
class FakeHandshakerService : public HandshakerService::Service {
 public:
  explicit FakeHandshakerService(const std::string& peer_identity)
      : peer_identity_(peer_identity) {}

  Status DoHandshake(
      ServerContext* /*server_context*/,
      ServerReaderWriter<HandshakerResp, HandshakerReq>* stream) override {
    Status status;
    HandshakerContext context;
    HandshakerReq request;
    HandshakerResp response;
    gpr_log(GPR_DEBUG, "Start a new handshake.");
    while (stream->Read(&request)) {
      status = ProcessRequest(&context, request, &response);
      if (!status.ok()) return WriteErrorResponse(stream, status);
      stream->Write(response);
      if (context.state == COMPLETED) return Status::OK;
      request.Clear();
    }
    return Status::OK;
  }

 private:
  // HandshakeState is used by fake handshaker server to keep track of client's
  // handshake status. In the beginning of a handshake, the state is INITIAL.
  // If start_client or start_server request is called, the state becomes at
  // least STARTED. When the handshaker server produces the first fame, the
  // state becomes SENT. After the handshaker server processes the final frame
  // from the peer, the state becomes COMPLETED.
  enum HandshakeState { INITIAL, STARTED, SENT, COMPLETED };

  struct HandshakerContext {
    bool is_client = true;
    HandshakeState state = INITIAL;
  };

  Status ProcessRequest(HandshakerContext* context,
                        const HandshakerReq& request,
                        HandshakerResp* response) {
    GPR_ASSERT(context != nullptr && response != nullptr);
    response->Clear();
    if (request.has_client_start()) {
      gpr_log(GPR_DEBUG, "Process client start request.");
      return ProcessClientStart(context, request.client_start(), response);
    } else if (request.has_server_start()) {
      gpr_log(GPR_DEBUG, "Process server start request.");
      return ProcessServerStart(context, request.server_start(), response);
    } else if (request.has_next()) {
      gpr_log(GPR_DEBUG, "Process next request.");
      return ProcessNext(context, request.next(), response);
    }
    return Status(StatusCode::INVALID_ARGUMENT, "Request is empty.");
  }

  Status ProcessClientStart(HandshakerContext* context,
                            const StartClientHandshakeReq& request,
                            HandshakerResp* response) {
    GPR_ASSERT(context != nullptr && response != nullptr);
    // Checks request.
    if (context->state != INITIAL) {
      return Status(StatusCode::FAILED_PRECONDITION, kWrongStateError);
    }
    if (request.application_protocols_size() == 0) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "At least one application protocol needed.");
    }
    if (request.record_protocols_size() == 0) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "At least one record protocol needed.");
    }
    // Sets response.
    response->set_out_frames(kClientInitFrame);
    response->set_bytes_consumed(0);
    response->mutable_status()->set_code(StatusCode::OK);
    // Updates handshaker context.
    context->is_client = true;
    context->state = SENT;
    return Status::OK;
  }

  Status ProcessServerStart(HandshakerContext* context,
                            const StartServerHandshakeReq& request,
                            HandshakerResp* response) {
    GPR_ASSERT(context != nullptr && response != nullptr);
    // Checks request.
    if (context->state != INITIAL) {
      return Status(StatusCode::FAILED_PRECONDITION, kWrongStateError);
    }
    if (request.application_protocols_size() == 0) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "At least one application protocol needed.");
    }
    if (request.handshake_parameters().empty()) {
      return Status(StatusCode::INVALID_ARGUMENT,
                    "At least one set of handshake parameters needed.");
    }
    // Sets response.
    if (request.in_bytes().empty()) {
      // start_server request does not have in_bytes.
      response->set_bytes_consumed(0);
      context->state = STARTED;
    } else {
      // start_server request has in_bytes.
      if (request.in_bytes() == kClientInitFrame) {
        response->set_out_frames(kServerFrame);
        response->set_bytes_consumed(strlen(kClientInitFrame));
        context->state = SENT;
      } else {
        return Status(StatusCode::UNKNOWN, kInvalidFrameError);
      }
    }
    response->mutable_status()->set_code(StatusCode::OK);
    context->is_client = false;
    return Status::OK;
  }

  Status ProcessNext(HandshakerContext* context,
                     const NextHandshakeMessageReq& request,
                     HandshakerResp* response) {
    GPR_ASSERT(context != nullptr && response != nullptr);
    if (context->is_client) {
      // Processes next request on client side.
      if (context->state != SENT) {
        return Status(StatusCode::FAILED_PRECONDITION, kWrongStateError);
      }
      if (request.in_bytes() != kServerFrame) {
        return Status(StatusCode::UNKNOWN, kInvalidFrameError);
      }
      response->set_out_frames(kClientFinishFrame);
      response->set_bytes_consumed(strlen(kServerFrame));
      context->state = COMPLETED;
    } else {
      // Processes next request on server side.
      HandshakeState current_state = context->state;
      if (current_state == STARTED) {
        if (request.in_bytes() != kClientInitFrame) {
          return Status(StatusCode::UNKNOWN, kInvalidFrameError);
        }
        response->set_out_frames(kServerFrame);
        response->set_bytes_consumed(strlen(kClientInitFrame));
        context->state = SENT;
      } else if (current_state == SENT) {
        // Client finish frame may be sent along with the first payload from the
        // client, handshaker only consumes the client finish frame.
        if (request.in_bytes().substr(0, strlen(kClientFinishFrame)) !=
            kClientFinishFrame) {
          return Status(StatusCode::UNKNOWN, kInvalidFrameError);
        }
        response->set_bytes_consumed(strlen(kClientFinishFrame));
        context->state = COMPLETED;
      } else {
        return Status(StatusCode::FAILED_PRECONDITION, kWrongStateError);
      }
    }
    // At this point, processing next request succeeded.
    response->mutable_status()->set_code(StatusCode::OK);
    if (context->state == COMPLETED) {
      *response->mutable_result() = GetHandshakerResult();
    }
    return Status::OK;
  }

  Status WriteErrorResponse(
      ServerReaderWriter<HandshakerResp, HandshakerReq>* stream,
      const Status& status) {
    GPR_ASSERT(!status.ok());
    HandshakerResp response;
    response.mutable_status()->set_code(status.error_code());
    response.mutable_status()->set_details(status.error_message());
    stream->Write(response);
    return status;
  }

  HandshakerResult GetHandshakerResult() {
    HandshakerResult result;
    result.set_application_protocol("grpc");
    result.set_record_protocol("ALTSRP_GCM_AES128_REKEY");
    result.mutable_peer_identity()->set_service_account(peer_identity_);
    result.mutable_local_identity()->set_service_account("local_identity");
    string key(1024, '\0');
    result.set_key_data(key);
    result.set_max_frame_size(16384);
    result.mutable_peer_rpc_versions()->mutable_max_rpc_version()->set_major(2);
    result.mutable_peer_rpc_versions()->mutable_max_rpc_version()->set_minor(1);
    result.mutable_peer_rpc_versions()->mutable_min_rpc_version()->set_major(2);
    result.mutable_peer_rpc_versions()->mutable_min_rpc_version()->set_minor(1);
    return result;
  }

  const std::string peer_identity_;
};

std::unique_ptr<grpc::Service> CreateFakeHandshakerService(
    const std::string& peer_identity) {
  return std::unique_ptr<grpc::Service>{
      new grpc::gcp::FakeHandshakerService(peer_identity)};
}

}  // namespace gcp
}  // namespace grpc
