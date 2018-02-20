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

#include "test/core/tsi/alts/frame_protector/alts_frame_protector_test_lib.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

const size_t kChannelSize = 32768;
const size_t kDefaultReadBufferAllocatedSize = 4096;
const size_t kDefaultMessageBufferAllocatedSize = 4096;
const size_t kDefaultProtectedBufferSize = 16384;
const size_t kDefaultMessageSize = 10;
const size_t kSmallReadBufferAllocatedSize = 41;
const size_t kSmallProtectedBufferSize = 37;
const size_t kSmallMessageBufferAllocatedSize = 42;
const size_t kSmallClientMaxOutputProtectedFrameSize = 39;
const size_t kSmallServerMaxOutputProtectedFrameSize = 43;
const size_t kLongMessageSize = 17000;

alts_test_config* alts_test_create_config(
    bool use_default_read_buffer_allocated_size,
    bool use_default_message_buffer_allocated_size,
    bool use_default_protected_buffer_size, bool use_default_client_message,
    bool use_default_server_message,
    bool use_default_client_max_output_protected_frame_size,
    bool use_default_server_max_output_protected_frame_size) {
  alts_test_config* config =
      static_cast<alts_test_config*>(gpr_malloc(sizeof(*config)));
  /* Set the value for read_buffer_allocated_size. */
  config->read_buffer_allocated_size = use_default_read_buffer_allocated_size
                                           ? kDefaultReadBufferAllocatedSize
                                           : kSmallReadBufferAllocatedSize;
  /* Set the value for message_buffer_allocated_size. */
  config->message_buffer_allocated_size =
      use_default_message_buffer_allocated_size
          ? kDefaultMessageBufferAllocatedSize
          : kSmallMessageBufferAllocatedSize;
  /* Set the value for protected_buffer_size. */
  config->protected_buffer_size = use_default_protected_buffer_size
                                      ? kDefaultProtectedBufferSize
                                      : kSmallProtectedBufferSize;
  /* Set the value for client message. */
  config->client_message_size =
      use_default_client_message ? kDefaultMessageSize : kLongMessageSize;
  /* Set the value for server message. */
  config->server_message_size =
      use_default_server_message ? kDefaultMessageSize : kLongMessageSize;
  /* Set the value for client max_output_protected_frame_size. */
  config->client_max_output_protected_frame_size =
      use_default_client_max_output_protected_frame_size
          ? 0
          : kSmallClientMaxOutputProtectedFrameSize;
  /* Set the value for server max_output_protected_frame_size. */
  config->server_max_output_protected_frame_size =
      use_default_server_max_output_protected_frame_size
          ? 0
          : kSmallServerMaxOutputProtectedFrameSize;
  /* Create and allocate memory for client and server channels. */
  config->client_channel = static_cast<uint8_t*>(gpr_malloc(kChannelSize));
  config->server_channel = static_cast<uint8_t*>(gpr_malloc(kChannelSize));
  config->bytes_written_to_client_channel = 0;
  config->bytes_written_to_server_channel = 0;
  config->bytes_read_from_client_channel = 0;
  config->bytes_read_from_server_channel = 0;
  return config;
}

void alts_test_set_config(alts_test_config* config,
                          size_t read_buffer_allocated_size,
                          size_t message_buffer_allocated_size,
                          size_t protected_buffer_size,
                          size_t client_max_output_protected_frame_size,
                          size_t server_max_output_protected_frame_size) {
  GPR_ASSERT(config != nullptr);
  config->read_buffer_allocated_size = read_buffer_allocated_size;
  config->message_buffer_allocated_size = message_buffer_allocated_size;
  config->protected_buffer_size = protected_buffer_size;
  config->client_max_output_protected_frame_size =
      client_max_output_protected_frame_size;
  config->server_max_output_protected_frame_size =
      server_max_output_protected_frame_size;
}

void alts_test_destroy_config(alts_test_config* config) {
  if (config != nullptr) {
    gpr_free(config->client_channel);
    gpr_free(config->server_channel);
    gpr_free(config);
  }
}

static void alts_test_send_bytes_to_peer(bool is_client, unsigned char* buf,
                                         size_t buf_size,
                                         alts_test_config* config) {
  GPR_ASSERT(config != nullptr && buf != nullptr);
  uint8_t* channel =
      is_client ? config->server_channel : config->client_channel;
  size_t* bytes_written = is_client ? &config->bytes_written_to_server_channel
                                    : &config->bytes_written_to_client_channel;
  /* Write data into channel. */
  memcpy(channel + *bytes_written, buf, buf_size);
  *bytes_written += buf_size;
}

static void alts_test_receive_bytes_to_peer(bool is_client, unsigned char* buf,
                                            size_t* buf_size,
                                            alts_test_config* config) {
  GPR_ASSERT(buf != nullptr && buf_size != nullptr && config != nullptr);
  uint8_t* channel =
      is_client ? config->client_channel : config->server_channel;
  size_t* bytes_read = is_client ? &config->bytes_read_from_client_channel
                                 : &config->bytes_read_from_server_channel;
  size_t* bytes_written = is_client ? &config->bytes_written_to_client_channel
                                    : &config->bytes_written_to_server_channel;
  size_t to_read = *buf_size < *bytes_written - *bytes_read
                       ? *buf_size
                       : *bytes_written - *bytes_read;
  /* Read data from channel. */
  memcpy(buf, channel + *bytes_read, to_read);
  *buf_size = to_read;
  *bytes_read += to_read;
}

static void alts_test_send_message_to_peer(bool is_client,
                                           tsi_frame_protector* protector,
                                           alts_test_config* config) {
  GPR_ASSERT(config != nullptr && protector != nullptr);
  unsigned char* protected_buffer =
      static_cast<unsigned char*>(gpr_malloc(config->protected_buffer_size));
  size_t message_size =
      is_client ? config->client_message_size : config->server_message_size;
  uint8_t* message =
      is_client ? config->client_message : config->server_message;
  const unsigned char* message_bytes =
      reinterpret_cast<const unsigned char*>(message);
  tsi_result result = TSI_OK;
  while (message_size > 0 && result == TSI_OK) {
    size_t protected_buffer_size_to_send = config->protected_buffer_size;
    size_t processed_message_size = message_size;
    result = tsi_frame_protector_protect(
        protector, message_bytes, &processed_message_size, protected_buffer,
        &protected_buffer_size_to_send);
    GPR_ASSERT(result == TSI_OK);
    alts_test_send_bytes_to_peer(is_client, protected_buffer,
                                 protected_buffer_size_to_send, config);
    message_bytes += processed_message_size;
    message_size -= processed_message_size;
    /* Flush if we're done. */
    if (message_size == 0) {
      size_t still_pending_size;
      do {
        protected_buffer_size_to_send = config->protected_buffer_size;
        result = tsi_frame_protector_protect_flush(
            protector, protected_buffer, &protected_buffer_size_to_send,
            &still_pending_size);
        GPR_ASSERT(result == TSI_OK);
        alts_test_send_bytes_to_peer(is_client, protected_buffer,
                                     protected_buffer_size_to_send, config);
      } while (still_pending_size > 0 && result == TSI_OK);
      GPR_ASSERT(result == TSI_OK);
    }
  }
  GPR_ASSERT(result == TSI_OK);
  gpr_free(protected_buffer);
}

static void alts_test_receive_message_from_peer(bool is_client,
                                                tsi_frame_protector* protector,
                                                alts_test_config* config,
                                                unsigned char* message,
                                                size_t* bytes_received) {
  GPR_ASSERT(protector != nullptr && config != nullptr && message != nullptr &&
             bytes_received != nullptr);
  size_t read_offset = 0;
  size_t message_offset = 0;
  size_t read_from_peer_size = 0;
  tsi_result result = TSI_OK;
  bool done = false;
  unsigned char* read_buffer = static_cast<unsigned char*>(
      gpr_malloc(config->read_buffer_allocated_size));
  unsigned char* message_buffer = static_cast<unsigned char*>(
      gpr_malloc(config->message_buffer_allocated_size));
  while (!done && result == TSI_OK) {
    if (read_from_peer_size == 0) {
      read_from_peer_size = config->read_buffer_allocated_size;
      alts_test_receive_bytes_to_peer(is_client, read_buffer,
                                      &read_from_peer_size, config);
      read_offset = 0;
    }
    if (read_from_peer_size == 0) done = true;
    size_t message_buffer_size;
    do {
      message_buffer_size = config->message_buffer_allocated_size;
      size_t processed_size = read_from_peer_size;
      result = tsi_frame_protector_unprotect(
          protector, read_buffer + read_offset, &processed_size, message_buffer,
          &message_buffer_size);
      GPR_ASSERT(result == TSI_OK);
      if (message_buffer_size > 0) {
        memcpy(message + message_offset, message_buffer, message_buffer_size);
        message_offset += message_buffer_size;
      }
      read_offset += processed_size;
      read_from_peer_size -= processed_size;
    } while ((read_from_peer_size > 0 || message_buffer_size > 0) &&
             result == TSI_OK);

    GPR_ASSERT(result == TSI_OK);
  }
  GPR_ASSERT(result == TSI_OK);
  *bytes_received = message_offset;

  gpr_free(read_buffer);
  gpr_free(message_buffer);
}

void alts_test_do_round_trip_check_frames(
    alts_test_config* config, const uint8_t* key, const size_t key_size,
    bool rekey, const uint8_t* client_message, const size_t client_message_size,
    const uint8_t* client_expected_frames, const size_t client_frame_size,
    const uint8_t* server_message, const size_t server_message_size,
    const uint8_t* server_expected_frames, const size_t server_frame_size) {
  GPR_ASSERT(config != nullptr);
  tsi_frame_protector* client_frame_protector = nullptr;
  tsi_frame_protector* server_frame_protector = nullptr;
  /* Create a client frame protector. */
  size_t client_max_output_protected_frame_size =
      config->client_max_output_protected_frame_size;
  GPR_ASSERT(
      alts_create_frame_protector(key, key_size, /*is_client=*/true, rekey,
                                  client_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &client_max_output_protected_frame_size,
                                  &client_frame_protector) == TSI_OK);
  /* Create a server frame protector. */
  size_t server_max_output_protected_frame_size =
      config->server_max_output_protected_frame_size;
  GPR_ASSERT(
      alts_create_frame_protector(key, key_size, /*is_client=*/false, rekey,
                                  server_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &server_max_output_protected_frame_size,
                                  &server_frame_protector) == TSI_OK);
  /* Client sends a message to server. */
  config->client_message = const_cast<uint8_t*>(client_message);
  config->client_message_size = client_message_size;
  alts_test_send_message_to_peer(/*is_client=*/true, client_frame_protector,
                                 config);
  /* Verify if the generated frame is the same as the expected. */
  GPR_ASSERT(config->bytes_written_to_server_channel == client_frame_size);
  GPR_ASSERT(memcmp(client_expected_frames, config->server_channel,
                    client_frame_size) == 0);
  unsigned char* server_received_message =
      static_cast<unsigned char*>(gpr_malloc(kChannelSize));
  size_t server_received_message_size = 0;
  alts_test_receive_message_from_peer(
      /*is_client=*/false, server_frame_protector, config,
      server_received_message, &server_received_message_size);
  GPR_ASSERT(config->client_message_size == server_received_message_size);
  GPR_ASSERT(memcmp(config->client_message, server_received_message,
                    server_received_message_size) == 0);
  /* Server sends a message to client. */
  config->server_message = const_cast<uint8_t*>(server_message);
  config->server_message_size = server_message_size;
  alts_test_send_message_to_peer(/*is_client=*/false, server_frame_protector,
                                 config);
  /* Verify if the generated frame is the same as the expected. */
  GPR_ASSERT(config->bytes_written_to_client_channel == server_frame_size);
  GPR_ASSERT(memcmp(server_expected_frames, config->client_channel,
                    server_frame_size) == 0);
  unsigned char* client_received_message =
      static_cast<unsigned char*>(gpr_malloc(kChannelSize));
  size_t client_received_message_size = 0;
  alts_test_receive_message_from_peer(
      /*is_client=*/true, client_frame_protector, config,
      client_received_message, &client_received_message_size);
  GPR_ASSERT(config->server_message_size == client_received_message_size);
  GPR_ASSERT(memcmp(config->server_message, client_received_message,
                    client_received_message_size) == 0);
  /* Destroy server and client frame protectors. */
  tsi_frame_protector_destroy(client_frame_protector);
  tsi_frame_protector_destroy(server_frame_protector);
  gpr_free(server_received_message);
  gpr_free(client_received_message);
}

void alts_test_do_round_trip(alts_test_config* config, bool rekey) {
  GPR_ASSERT(config != nullptr);
  tsi_frame_protector* client_frame_protector = nullptr;
  tsi_frame_protector* server_frame_protector = nullptr;
  /* Create a key to be used by both client and server. */
  uint8_t* key = nullptr;
  size_t key_length = rekey ? kAes128GcmRekeyKeyLength : kAes128GcmKeyLength;
  gsec_test_random_array(&key, key_length);
  /* Create a client frame protector. */
  size_t client_max_output_protected_frame_size =
      config->client_max_output_protected_frame_size;
  GPR_ASSERT(
      alts_create_frame_protector(key, key_length, /*is_client=*/true, rekey,
                                  client_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &client_max_output_protected_frame_size,
                                  &client_frame_protector) == TSI_OK);
  /* Create a server frame protector. */
  size_t server_max_output_protected_frame_size =
      config->server_max_output_protected_frame_size;
  GPR_ASSERT(
      alts_create_frame_protector(key, key_length, /*is_client=*/false, rekey,
                                  server_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &server_max_output_protected_frame_size,
                                  &server_frame_protector) == TSI_OK);
  /* Client sends a message to server. */
  gsec_test_random_array(&config->client_message, config->client_message_size);
  alts_test_send_message_to_peer(/*is_client=*/true, client_frame_protector,
                                 config);
  unsigned char* server_received_message =
      static_cast<unsigned char*>(gpr_malloc(kChannelSize));
  size_t server_received_message_size = 0;
  alts_test_receive_message_from_peer(
      /*is_client=*/false, server_frame_protector, config,
      server_received_message, &server_received_message_size);
  GPR_ASSERT(config->client_message_size == server_received_message_size);
  GPR_ASSERT(memcmp(config->client_message, server_received_message,
                    server_received_message_size) == 0);
  /* Server sends a message to client. */
  gsec_test_random_array(&config->server_message, config->server_message_size);
  alts_test_send_message_to_peer(/*is_client=*/false, server_frame_protector,
                                 config);
  unsigned char* client_received_message =
      static_cast<unsigned char*>(gpr_malloc(kChannelSize));
  size_t client_received_message_size = 0;
  alts_test_receive_message_from_peer(
      /*is_client=*/true, client_frame_protector, config,
      client_received_message, &client_received_message_size);
  GPR_ASSERT(config->server_message_size == client_received_message_size);
  GPR_ASSERT(memcmp(config->server_message, client_received_message,
                    client_received_message_size) == 0);
  /* Destroy server and client frame protectors. */
  tsi_frame_protector_destroy(client_frame_protector);
  tsi_frame_protector_destroy(server_frame_protector);
  gpr_free(config->client_message);
  gpr_free(config->server_message);
  gpr_free(server_received_message);
  gpr_free(client_received_message);
  gpr_free(key);
}

void alts_test_do_ping_pong(bool rekey) {
  unsigned char to_server[4096];
  unsigned char to_client[4096];
  size_t max_frame_size = sizeof(to_client);
  const char ping_request[] = "Ping";
  const char pong_response[] = "Pong";
  tsi_frame_protector* client_frame_protector;
  tsi_frame_protector* server_frame_protector;
  /* Create a key to be used by both client and server. */
  uint8_t* key = nullptr;
  size_t key_length = rekey ? kAes128GcmRekeyKeyLength : kAes128GcmKeyLength;
  gsec_test_random_array(&key, key_length);
  GPR_ASSERT(alts_create_frame_protector(key, key_length,
                                         /*is_client=*/true, rekey,
                                         &max_frame_size,
                                         &client_frame_protector) == TSI_OK);
  GPR_ASSERT(max_frame_size == sizeof(to_client));
  GPR_ASSERT(alts_create_frame_protector(key, key_length,
                                         /*is_client=*/false, rekey,
                                         &max_frame_size,
                                         &server_frame_protector) == TSI_OK);
  GPR_ASSERT(max_frame_size == sizeof(to_client));
  /* Client sends a ping request. */
  size_t ping_length = strlen(ping_request);
  size_t protected_size = sizeof(to_server);
  GPR_ASSERT(tsi_frame_protector_protect(
                 client_frame_protector,
                 reinterpret_cast<const unsigned char*>(ping_request),
                 &ping_length, to_server, &protected_size) == TSI_OK);
  GPR_ASSERT(ping_length == strlen(ping_request));
  GPR_ASSERT(protected_size == 0);
  protected_size = sizeof(to_server);
  size_t still_pending_size;
  GPR_ASSERT(tsi_frame_protector_protect_flush(client_frame_protector,
                                               to_server, &protected_size,
                                               &still_pending_size) == TSI_OK);
  GPR_ASSERT(still_pending_size == 0);
  GPR_ASSERT(protected_size > strlen(ping_request));
  /* Server receives a ping request. */
  size_t unprotected_size = sizeof(to_server);
  size_t saved_protected_size = protected_size;
  GPR_ASSERT(tsi_frame_protector_unprotect(server_frame_protector, to_server,
                                           &protected_size, to_server,
                                           &unprotected_size) == TSI_OK);
  GPR_ASSERT(saved_protected_size == protected_size);
  GPR_ASSERT(ping_length == unprotected_size);
  GPR_ASSERT(memcmp(ping_request, to_server, unprotected_size) == 0);
  /* Server sends back a pong response. */
  size_t pong_length = strlen(pong_response);
  protected_size = sizeof(to_client);
  GPR_ASSERT(tsi_frame_protector_protect(
                 server_frame_protector,
                 reinterpret_cast<const unsigned char*>(pong_response),
                 &pong_length, to_client, &protected_size) == TSI_OK);
  GPR_ASSERT(pong_length == strlen(pong_response));
  GPR_ASSERT(protected_size == 0);
  protected_size = sizeof(to_client);
  GPR_ASSERT(tsi_frame_protector_protect_flush(server_frame_protector,
                                               to_client, &protected_size,
                                               &still_pending_size) == TSI_OK);
  GPR_ASSERT(still_pending_size == 0);
  GPR_ASSERT(protected_size > strlen(pong_response));
  /* Client receives a pong response. */
  unprotected_size = sizeof(to_server);
  saved_protected_size = protected_size;
  GPR_ASSERT(tsi_frame_protector_unprotect(client_frame_protector, to_client,
                                           &protected_size, to_client,
                                           &unprotected_size) == TSI_OK);
  GPR_ASSERT(saved_protected_size == protected_size);
  GPR_ASSERT(pong_length == unprotected_size);
  GPR_ASSERT(memcmp(pong_response, to_client, unprotected_size) == 0);

  tsi_frame_protector_destroy(client_frame_protector);
  tsi_frame_protector_destroy(server_frame_protector);

  gpr_free(key);
}
