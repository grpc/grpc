// Copyright 2024 The gRPC Authors.
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

#include "src/core/tsi/alts/frame_protector/alts_frame_protector.h"

#include <grpc/support/alloc.h>

#include "test/core/tsi/alts/crypt/gsec_test_util.h"
#include "test/core/tsi/transport_security_test_lib.h"

bool squelch = true;
bool leak_check = true;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* key, size_t key_length) {
  tsi_test_frame_protector_fixture* fixture =
      tsi_test_frame_protector_fixture_create();
  tsi_frame_protector* client_frame_protector = nullptr;
  tsi_frame_protector* server_frame_protector = nullptr;
  tsi_test_frame_protector_config* config = fixture->config;
  tsi_test_channel* channel = fixture->channel;
  // Create a client frame protector.
  size_t client_max_output_protected_frame_size =
      config->client_max_output_protected_frame_size;
  if (alts_create_frame_protector(key, key_length, /*is_client=*/true,
                                  /*is_rekey=*/false,
                                  client_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &client_max_output_protected_frame_size,
                                  &client_frame_protector) != TSI_OK) {
    tsi_test_frame_protector_fixture_destroy(fixture);
    return 0;
  }
  // Create a server frame protector.
  size_t server_max_output_protected_frame_size =
      config->server_max_output_protected_frame_size;
  if (alts_create_frame_protector(key, key_length, /*is_client=*/false,
                                  /*is_rekey=*/false,
                                  server_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &server_max_output_protected_frame_size,
                                  &server_frame_protector) != TSI_OK) {
    tsi_test_frame_protector_fixture_destroy(fixture);
    return 0;
  }
  tsi_test_frame_protector_fixture_init(fixture, client_frame_protector,
                                        server_frame_protector);
  // Client sends a message to server.
  tsi_test_frame_protector_send_message_to_peer(config, channel,
                                                client_frame_protector,
                                                /*is_client=*/true);
  unsigned char* server_received_message =
      static_cast<unsigned char*>(gpr_malloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  size_t server_received_message_size = 0;
  tsi_test_frame_protector_receive_message_from_peer(
      config, channel, server_frame_protector, server_received_message,
      &server_received_message_size, /*is_client=*/false);
  // Check server received the same message as client sent.
  if (config->client_message_size != server_received_message_size) {
    abort();
  }
  if (memcmp(config->client_message, server_received_message,
             server_received_message_size) != 0) {
    abort();
  }
  // Server sends a message to client.
  tsi_test_frame_protector_send_message_to_peer(config, channel,
                                                server_frame_protector,
                                                /*is_client=*/false);
  unsigned char* client_received_message =
      static_cast<unsigned char*>(gpr_malloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  size_t client_received_message_size = 0;
  tsi_test_frame_protector_receive_message_from_peer(
      config, channel, client_frame_protector, client_received_message,
      &client_received_message_size,
      /*is_client=*/true);
  // Check client received the same message as server sent.
  if (config->server_message_size != client_received_message_size) {
    abort();
  }
  if (memcmp(config->server_message, client_received_message,
             client_received_message_size) != 0) {
    abort();
  }
  // Destroy server and client frame protectors.
  gpr_free(server_received_message);
  gpr_free(client_received_message);
  tsi_test_frame_protector_fixture_destroy(fixture);
  return 0;
}
