/*
 *
 * Copyright 2018 gRPC authors.
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

#ifndef GRPC_TEST_CORE_TSI_ALTS_FRAME_PROTECTOR_ALTS_FRAME_PROTECTOR_TEST_LIB_H
#define GRPC_TEST_CORE_TSI_ALTS_FRAME_PROTECTOR_ALTS_FRAME_PROTECTOR_TEST_LIB_H

/**
 * TODO: Use TSI test library in
 * third_party/grpc/test/core/tsi/transport_security_test_lib.h. after exposing
 * common APIs that can be shared by both handshake and record protocol
 * operations.
 */
#include <stdbool.h>

#include <grpc/support/log.h>

#include "src/core/tsi/alts/crypt/gsec.h"
#include "src/core/tsi/alts/frame_protector/alts_frame_protector.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/tsi/alts/crypt/gsec_test_util.h"

/* Main struct for alts_test_config. */
typedef struct alts_test_config {
  size_t read_buffer_allocated_size;
  size_t message_buffer_allocated_size;
  size_t protected_buffer_size;
  uint8_t* client_message;
  uint8_t* server_message;
  size_t client_message_size;
  size_t server_message_size;
  size_t client_max_output_protected_frame_size;
  size_t server_max_output_protected_frame_size;
  uint8_t* client_channel;
  uint8_t* server_channel;
  size_t bytes_written_to_client_channel;
  size_t bytes_written_to_server_channel;
  size_t bytes_read_from_client_channel;
  size_t bytes_read_from_server_channel;
} alts_test_config;

alts_test_config* alts_test_create_config(
    bool use_default_read_buffer_allocated_size,
    bool use_default_message_buffer_allocated_size,
    bool use_default_protected_buffer_size, bool use_default_client_message,
    bool use_default_server_message,
    bool use_default_client_max_output_protected_frame_size,
    bool use_default_server_max_output_protected_frame_size);

void alts_test_set_config(alts_test_config* config,
                          size_t read_buffer_allocated_size,
                          size_t message_buffer_allocated_size,
                          size_t protected_buffer_size,
                          size_t client_max_output_protected_frame_size,
                          size_t server_max_output_protected_frame_size);

void alts_test_destroy_config(alts_test_config* config);

void alts_test_do_ping_pong(bool rekey);

void alts_test_do_round_trip_check_frames(
    alts_test_config* config, const uint8_t* key, const size_t key_size,
    bool rekey, const uint8_t* client_message, const size_t client_message_size,
    const uint8_t* client_expected_frames, const size_t client_frame_size,
    const uint8_t* server_message, const size_t server_message_size,
    const uint8_t* server_expected_frames, const size_t server_frame_size);

void alts_test_do_round_trip(alts_test_config* config, bool rekey);

#endif  // GRPC_TEST_CORE_TSI_ALTS_FRAME_PROTECTOR_ALTS_FRAME_PROTECTOR_TEST_LIB_H
