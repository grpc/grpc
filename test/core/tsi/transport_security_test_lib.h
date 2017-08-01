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

#define TSI_TEST_TINY_HANDSHAKE_BUFFER_SIZE 1
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

/* Main struct for transport_security_test_lib. */
typedef struct transport_security_test_lib transport_security_test_lib;

/* V-table for transport_security_test_lib operations. */
typedef struct transport_security_test_vtable {
  void (*setup_handshakers)(transport_security_test_lib *lib);
  void (*check_handshake_results)(transport_security_test_lib *lib);
  void (*destruct)(transport_security_test_lib *lib);
} tranport_security_test_vtable;

/* Main struct for transport_security_test_config. */
typedef struct transport_security_test_config transport_security_test_config;

struct transport_security_test_lib {
  const struct transport_security_test_vtable *vtable;
  transport_security_test_config *config;
  tsi_handshaker *client_handshaker;
  tsi_handshaker *server_handshaker;
  tsi_handshaker_result *client_result;
  tsi_handshaker_result *server_result;
};

struct transport_security_test_config {
  size_t read_buffer_allocated_size;
  size_t message_buffer_allocated_size;
  size_t protected_buffer_size;
  size_t handshake_buffer_size;
  size_t client_max_output_protected_frame_size;
  size_t server_max_output_protected_frame_size;
  uint8_t *client_message;
  uint8_t *server_message;
  size_t client_message_size;
  size_t server_message_size;
  uint8_t *client_channel;
  uint8_t *server_channel;
  size_t bytes_written_to_client_channel;
  size_t bytes_written_to_server_channel;
  size_t bytes_read_from_client_channel;
  size_t bytes_read_from_server_channel;
  uint8_t *big_message;
  uint8_t *small_message;
};

/* This method creates a transport_security_test_config instance. */
transport_security_test_config *transport_security_test_config_create(
    bool use_default_read_buffer_allocated_size,
    bool use_default_message_buffer_allocated_size,
    bool use_default_protected_buffer_size, bool use_default_client_message,
    bool use_default_server_message,
    bool use_default_client_max_output_protected_frame_size,
    bool use_default_server_max_output_protected_frame_size,
    bool use_default_handshake_buffer_size);

/* This method sets member values of test config instance. */
void transport_security_test_config_set_buffer_size(
    transport_security_test_config *config, size_t read_buffer_allocated_size,
    size_t message_buffer_allocated_size, size_t protected_buffer_size,
    size_t client_max_output_protected_frame_size,
    size_t server_max_output_protected_frame_size);

/* This method destroys a transport_security_test_config instance. */
void transport_security_test_config_destroy(
    transport_security_test_config *config);

/* This method destroys a transport_security_test_lib instance. */
void transport_security_test_destroy(transport_security_test_lib *lib);

/* This method performs a full TSI handshake. */
void transport_security_test_do_handshake(transport_security_test_lib *lib);

/* This method performs a ping pong test. */
void transport_security_test_do_ping_pong(transport_security_test_lib *lib);

/* This method performs a round trip test with a default test config. */
void transport_security_test_do_round_trip(transport_security_test_lib *lib);

#endif  // GRPC_TEST_CORE_TSI_TRANSPORT_SECURITY_TEST_LIB_H_
