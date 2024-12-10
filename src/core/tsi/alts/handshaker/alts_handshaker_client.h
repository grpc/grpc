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

#ifndef GRPC_SRC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_CLIENT_H
#define GRPC_SRC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_CLIENT_H

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/pollset_set.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/core/tsi/transport_security_interface.h"

#define ALTS_SERVICE_METHOD "/grpc.gcp.HandshakerService/DoHandshake"
#define ALTS_APPLICATION_PROTOCOL "grpc"
#define ALTS_RECORD_PROTOCOL "ALTSRP_GCM_AES128_REKEY"
#define ALTS_HANDSHAKER_SERVICE_URL_FOR_TESTING "lame"

// A function that makes the grpc call to the handshaker service.
typedef grpc_call_error (*alts_grpc_caller)(grpc_call* call, const grpc_op* ops,
                                            size_t nops, grpc_closure* tag);

///
/// A ALTS handshaker client interface. It is used to communicate with
/// ALTS handshaker service by scheduling a handshaker request that could be one
/// of client_start, server_start, and next handshaker requests. All APIs in the
/// header are thread-compatible.
///
class AltsHandshakerClient {
 public:
  ///
  /// This factory method creates an ALTS handshaker client.
  ///
  ///- handshaker: ALTS TSI handshaker to which the created handshaker client
  ///  belongs to.
  ///- channel: grpc channel to ALTS handshaker service.
  ///- handshaker_service_url: address of ALTS handshaker service in the format
  /// of
  ///  "host:port".
  ///- interested_parties: set of pollsets interested in this connection.
  ///- options: ALTS credentials options containing information passed from TSI
  ///  caller (e.g., rpc protocol versions)
  ///- target_name: the name of the endpoint that the channel is connecting to,
  ///  and will be used for secure naming check
  ///- grpc_cb: gRPC provided callbacks passed from TSI handshaker.
  ///- cb: callback to be executed when tsi_handshaker_next API compltes.
  ///- user_data: argument passed to cb.
  ///- vtable_for_testing: ALTS handshaker client vtable instance used for
  ///  testing purpose.
  ///- is_client: a boolean value indicating if the created handshaker client is
  ///  used at the client (is_client = true) or server (is_client = false) side.
  ///- max_frame_size: Maximum frame size used by frame protector (User
  /// specified
  ///  maximum frame size if present or default max frame size).
  ///
  /// It returns the created ALTS handshaker client on success, and NULL
  /// on failure.
  ///
  static AltsHandshakerClient* alts_grpc_handshaker_client_create(
      alts_tsi_handshaker* handshaker, grpc_channel* channel,
      const char* handshaker_service_url, grpc_pollset_set* interested_parties,
      grpc_alts_credentials_options* options, const grpc_slice& target_name,
      grpc_iomgr_cb_func grpc_cb, tsi_handshaker_on_next_done_cb cb,
      void* user_data, alts_handshaker_client_vtable* vtable_for_testing,
      bool is_client, size_t max_frame_size, std::string* error);

  ///
  /// This method cancels previously scheduled, but yet executed handshaker
  /// requests to ALTS handshaker service. After this operation, the handshake
  /// will be shutdown, and no more handshaker requests will get scheduled.
  ///
  ///- client: ALTS handshaker client instance.
  ///
  static void alts_handshaker_client_shutdown(AltsHandshakerClient* client);

  ///
  /// This method destroys an ALTS handshaker client.
  ///
  ///- client: an ALTS handshaker client instance.
  ///
  static void alts_handshaker_client_destroy(AltsHandshakerClient* client);

  ///
  /// This method schedules a client_start handshaker request to ALTS handshaker
  /// service.
  ///
  ///
  /// It returns TSI_OK on success and an error status code on failure.
  ///
  static tsi_result alts_handshaker_client_start_client(AltsHandshakerClient* client);

  ///
  /// This method schedules a server_start handshaker request to ALTS handshaker
  /// service.
  ///
  ///- bytes_received: bytes in out_frames returned from the peer's handshaker
  ///  response.
  ///
  /// It returns TSI_OK on success and an error status code on failure.
  ///
  static tsi_result alts_handshaker_client_start_server(AltsHandshakerClient* client, grpc_slice* bytes_received);

  ///
  /// This method schedules a next handshaker request to ALTS handshaker
  /// service.
  ///
  ///- bytes_received: bytes in out_frames returned from the peer's handshaker
  ///  response.
  ///
  /// It returns TSI_OK on success and an error status code on failure.
  ///
  static tsi_result alts_handshaker_client_next(AltsHandshakerClient* client, grpc_slice* bytes_received);

  ///
  /// This method handles handshaker response returned from ALTS handshaker
  /// service. Note that the only reason the API is exposed is that it is used
  /// in alts_shared_resources.cc.
  ///
  ///- is_ok: a boolean value indicating if the handshaker response is ok to
  /// read.
  ///
  static void alts_handshaker_client_handle_response(AltsHandshakerClient* client, bool is_ok);

  // Returns the max number of concurrent handshakes that are permitted.
  //
  // Exposed for testing purposes only.
  static size_t MaxNumberOfConcurrentHandshakes();

 private:
  static constexpr size_t kAltsAes128GcmRekeyKeyLength = 44;
  static constexpr char kMaxConcurrentStreamsEnvironmentVariable[] =
      "GRPC_ALTS_MAX_CONCURRENT_HANDSHAKES";
  static constexpr int kHandshakerClientOpNum = 4;

  AltsHandshakerClient();
  ~AltsHandshakerClient();
  void handshaker_client_send_buffer_destroy();
  static bool is_handshake_finished_properly(grpc_gcp_HandshakerResp* resp);
  void maybe_complete_tsi_next(
      bool receive_status_finished,
      recv_message_result* pending_recv_message_result);
  void handle_response_done(tsi_result status, std::string error,
                            const unsigned char* bytes_to_send,
                            size_t bytes_to_send_size,
                            tsi_handshaker_result* result);
  tsi_result continue_make_grpc_call(bool is_start);
  tsi_result make_grpc_call(bool is_start);
  static void on_status_received(void* arg, grpc_error_handle error);
  static grpc_byte_buffer* get_serialized_handshaker_req(
      grpc_gcp_HandshakerReq* req, upb_Arena* arena);
  grpc_byte_buffer* get_serialized_start_client();
  tsi_result handshaker_client_start_client();
  grpc_byte_buffer* get_serialized_start_server(grpc_slice* bytes_received);
  tsi_result handshaker_client_start_server(grpc_slice* bytes_received);
  static grpc_byte_buffer* get_serialized_next(grpc_slice* bytes_received);
  tsi_result handshaker_client_next(grpc_slice* bytes_received);
  void handshaker_client_shutdown();
  void handshaker_call_unref(void* arg, grpc_error_handle /* error */);
  void handshaker_client_destruct();
  
  struct recv_message_result;
  // One ref is held by the entity that created this handshaker_client, and
  // another ref is held by the pending RECEIVE_STATUS_ON_CLIENT op.
  gpr_refcount refs;
  alts_tsi_handshaker* handshaker;
  grpc_call* call;
  // A pointer to a function handling the interaction with handshaker service.
  // That is, it points to grpc_call_start_batch_and_execute when the handshaker
  // client is used in a non-testing use case and points to a custom function
  // that validates the data to be sent to handshaker service in a testing use
  // case.
  alts_grpc_caller grpc_caller;
  // A gRPC closure to be scheduled when the response from handshaker service
  // is received. It will be initialized with the injected grpc RPC callback.
  grpc_closure on_handshaker_service_resp_recv;
  // Buffers containing information to be sent (or received) to (or from) the
  // handshaker service.
  grpc_byte_buffer* send_buffer = nullptr;
  grpc_byte_buffer* recv_buffer = nullptr;
  // Used to inject a read failure from tests.
  bool inject_read_failure = false;
  // Initial metadata to be received from handshaker service.
  grpc_metadata_array recv_initial_metadata;
  // A callback function provided by an application to be invoked when response
  // is received from handshaker service.
  tsi_handshaker_on_next_done_cb cb;
  void* user_data;
  // ALTS credential options passed in from the caller.
  grpc_alts_credentials_options* options;
  // target name information to be passed to handshaker service for server
  // authorization check.
  grpc_slice target_name;
  // boolean flag indicating if the handshaker client is used at client
  // (is_client = true) or server (is_client = false) side.
  bool is_client;
  // a temporary store for data received from handshaker service used to extract
  // unused data.
  grpc_slice recv_bytes;
  // a buffer containing data to be sent to the grpc client or server's peer.
  unsigned char* buffer;
  size_t buffer_size;
  /// callback for receiving handshake call status
  grpc_closure on_status_received;
  /// gRPC status code of handshake call
  grpc_status_code handshake_status_code = GRPC_STATUS_OK;
  /// gRPC status details of handshake call
  grpc_slice handshake_status_details;
  // mu synchronizes all fields below including their internal fields.
  grpc_core::Mutex mu;
  // indicates if the handshaker call's RECV_STATUS_ON_CLIENT op is done.
  bool receive_status_finished = false;
  // if non-null, contains arguments to complete a TSI next callback.
  recv_message_result* pending_recv_message_result = nullptr;
  // Maximum frame size used by frame protector.
  size_t max_frame_size;
  // If non-null, will be populated with an error string upon error.
  std::string* error;
};

#endif  // GRPC_SRC_CORE_TSI_ALTS_HANDSHAKER_ALTS_HANDSHAKER_CLIENT_H
