/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_TEST_CORE_TSI_TRANSPORT_SECURITY_TEST_LIB_H_
#define GRPC_TEST_CORE_TSI_TRANSPORT_SECURITY_TEST_LIB_H_

#include "src/core/tsi/transport_security_interface.h"

#include <grpc/support/sync.h>

#define TSI_TEST_TINY_HANDSHAKE_BUFFER_SIZE 32
#define TSI_TEST_SMALL_HANDSHAKE_BUFFER_SIZE 128
#define TSI_TEST_SMALL_READ_BUFFER_ALLOCATED_SIZE 41
#define TSI_TEST_SMALL_PROTECTED_BUFFER_SIZE 37
#define TSI_TEST_SMALL_MESSAGE_BUFFER_ALLOCATED_SIZE 42
#define TSI_TEST_SMALL_CLIENT_MAX_OUTPUT_PROTECTED_FRAME_SIZE 39
#define TSI_TEST_SMALL_SERVER_MAX_OUTPUT_PROTECTED_FRAME_SIZE 43
#define TSI_TEST_DEFAULT_BUFFER_SIZE 4096
#define TSI_TEST_DEFAULT_PROTECTED_BUFFER_SIZE 16384
#define TSI_TEST_DEFAULT_CHANNEL_SIZE 32768
#define TSI_TEST_BIG_MESSAGE_SIZE 17000
#define TSI_TEST_SMALL_MESSAGE_SIZE 10
#define TSI_TEST_NUM_OF_ARGUMENTS 8
#define TSI_TEST_NUM_OF_COMBINATIONS 256
#define TSI_TEST_UNUSED_BYTES "HELLO GOOGLE"

/* ---  tsi_test_fixture object ---
  The tests for specific TSI implementations should create their own
  custom "subclass" of this fixture, which wraps all information
  that will be used to test correctness of TSI handshakes and frame
  protect/unprotect operations with respect to TSI implementations. */
typedef struct tsi_test_fixture tsi_test_fixture;

/* ---  tsi_test_frame_protector_config object ---

  This object is used to configure different parameters of TSI frame protector
  APIs. */
typedef struct tsi_test_frame_protector_config tsi_test_frame_protector_config;

/* V-table for tsi_test_fixture operations that are implemented differently in
   different TSI implementations. */
typedef struct tsi_test_fixture_vtable {
  void (*setup_handshakers)(tsi_test_fixture* fixture);
  void (*check_handshaker_peers)(tsi_test_fixture* fixture);
  void (*destruct)(tsi_test_fixture* fixture);
} tsi_test_fixture_vtable;

struct tsi_test_fixture {
  const tsi_test_fixture_vtable* vtable;
  /* client/server TSI handshaker used to perform TSI handshakes, and will get
     instantiated during the call to setup_handshakers. */
  tsi_handshaker* client_handshaker;
  tsi_handshaker* server_handshaker;
  /* client/server TSI handshaker results used to store the result of TSI
     handshake. If the handshake fails, the result will store NULL upon
     finishing the handshake. */
  tsi_handshaker_result* client_result;
  tsi_handshaker_result* server_result;
  /* size of buffer used to store data received from the peer. */
  size_t handshake_buffer_size;
  /* simulated channels between client and server. If the server (client)
     wants to send data to the client (server), he will write data to
     client_channel (server_channel), which will be read by client (server). */
  uint8_t* client_channel;
  uint8_t* server_channel;
  /* size of data written to the client/server channel. */
  size_t bytes_written_to_client_channel;
  size_t bytes_written_to_server_channel;
  /* size of data read from the client/server channel */
  size_t bytes_read_from_client_channel;
  size_t bytes_read_from_server_channel;
  /* tsi_test_frame_protector_config instance */
  tsi_test_frame_protector_config* config;
  /* a flag indicating if client has finished TSI handshake first (i.e., before
     server).
     The flag should be referred if and only if TSI handshake finishes
     successfully. */
  bool has_client_finished_first;
  /* a flag indicating whether to test tsi_handshaker_result_get_unused_bytes()
     for TSI implementation. This field is true by default, and false
     for SSL TSI implementation due to grpc issue #12164
     (https://github.com/grpc/grpc/issues/12164).
  */
  bool test_unused_bytes;
  /* These objects will be used coordinate client/server handshakers with TSI
     thread to perform TSI handshakes in an asynchronous manner (for GTS TSI
     implementations).
  */
  gpr_cv cv;
  gpr_mu mu;
  bool notified;
};

struct tsi_test_frame_protector_config {
  /* size of buffer used to store protected frames to be unprotected. */
  size_t read_buffer_allocated_size;
  /* size of buffer used to store bytes resulted from unprotect operations. */
  size_t message_buffer_allocated_size;
  /* size of buffer used to store frames resulted from protect operations. */
  size_t protected_buffer_size;
  /* size of client/server maximum frame size. */
  size_t client_max_output_protected_frame_size;
  size_t server_max_output_protected_frame_size;
  /* pointer that points to client/server message to be protected. */
  uint8_t* client_message;
  uint8_t* server_message;
  /* size of client/server message. */
  size_t client_message_size;
  size_t server_message_size;
};

/* This method creates a tsi_test_frame_protector_config instance. Each
   parameter of this function is a boolean value indicating whether to set the
   corresponding parameter with a default value or not. If it's false, it will
   be set with a specific value which is usually much smaller than the default.
   Both values are defined with #define directive. */
tsi_test_frame_protector_config* tsi_test_frame_protector_config_create(
    bool use_default_read_buffer_allocated_size,
    bool use_default_message_buffer_allocated_size,
    bool use_default_protected_buffer_size, bool use_default_client_message,
    bool use_default_server_message,
    bool use_default_client_max_output_protected_frame_size,
    bool use_default_server_max_output_protected_frame_size,
    bool use_default_handshake_buffer_size);

/* This method sets different buffer and frame sizes of a
   tsi_test_frame_protector_config instance with user provided values. */
void tsi_test_frame_protector_config_set_buffer_size(
    tsi_test_frame_protector_config* config, size_t read_buffer_allocated_size,
    size_t message_buffer_allocated_size, size_t protected_buffer_size,
    size_t client_max_output_protected_frame_size,
    size_t server_max_output_protected_frame_size);

/* This method destroys a tsi_test_frame_protector_config instance. */
void tsi_test_frame_protector_config_destroy(
    tsi_test_frame_protector_config* config);

/* This method initializes members of tsi_test_fixture instance.
   Note that the struct instance should be allocated before making
   this call. */
void tsi_test_fixture_init(tsi_test_fixture* fixture);

/* This method destroys a tsi_test_fixture instance. Note that the
   fixture intance must be dynamically allocated and will be freed by
   this function. */
void tsi_test_fixture_destroy(tsi_test_fixture* fixture);

/* This method performs a full TSI handshake between a client and a server.
   Note that the test library will implement the new TSI handshaker API to
   perform handshakes. */
void tsi_test_do_handshake(tsi_test_fixture* fixture);

/* This method performs a round trip test between the client and the server.
   That is, the client sends a protected message to a server who receives the
   message, and unprotects it. The same operation is triggered again with
   the client and server switching its role. */
void tsi_test_do_round_trip(tsi_test_fixture* fixture);

#endif  // GRPC_TEST_CORE_TSI_TRANSPORT_SECURITY_TEST_LIB_H_
