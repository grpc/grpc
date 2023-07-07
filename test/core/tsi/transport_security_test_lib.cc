//
//
// Copyright 2017 gRPC authors.
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

#include "test/core/tsi/transport_security_test_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/base.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/digest.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/security/transport/tsi_error.h"

static void notification_signal(tsi_test_fixture* fixture) {
  gpr_mu_lock(&fixture->mu);
  fixture->notified = true;
  gpr_cv_signal(&fixture->cv);
  gpr_mu_unlock(&fixture->mu);
}

static void notification_wait(tsi_test_fixture* fixture) {
  gpr_mu_lock(&fixture->mu);
  while (!fixture->notified) {
    gpr_cv_wait(&fixture->cv, &fixture->mu, gpr_inf_future(GPR_CLOCK_REALTIME));
  }
  fixture->notified = false;
  gpr_mu_unlock(&fixture->mu);
}

typedef struct handshaker_args {
  tsi_test_fixture* fixture;
  unsigned char* handshake_buffer;
  size_t handshake_buffer_size;
  bool is_client;
  bool transferred_data;
  bool appended_unused_bytes;
  grpc_error_handle error;
} handshaker_args;

static handshaker_args* handshaker_args_create(tsi_test_fixture* fixture,
                                               bool is_client) {
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(fixture->config != nullptr);
  handshaker_args* args = new handshaker_args();
  args->fixture = fixture;
  args->handshake_buffer_size = fixture->handshake_buffer_size;
  args->handshake_buffer =
      static_cast<unsigned char*>(gpr_zalloc(args->handshake_buffer_size));
  args->is_client = is_client;
  args->error = absl::OkStatus();
  return args;
}

static void handshaker_args_destroy(handshaker_args* args) {
  gpr_free(args->handshake_buffer);
  delete args;
}

static void do_handshaker_next(handshaker_args* args);

static void setup_handshakers(tsi_test_fixture* fixture) {
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(fixture->vtable != nullptr);
  GPR_ASSERT(fixture->vtable->setup_handshakers != nullptr);
  fixture->vtable->setup_handshakers(fixture);
}

static void check_unused_bytes(tsi_test_fixture* fixture) {
  tsi_handshaker_result* result_with_unused_bytes =
      fixture->has_client_finished_first ? fixture->server_result
                                         : fixture->client_result;
  tsi_handshaker_result* result_without_unused_bytes =
      fixture->has_client_finished_first ? fixture->client_result
                                         : fixture->server_result;
  const unsigned char* bytes = nullptr;
  size_t bytes_size = 0;
  GPR_ASSERT(tsi_handshaker_result_get_unused_bytes(
                 result_with_unused_bytes, &bytes, &bytes_size) == TSI_OK);
  GPR_ASSERT(bytes_size == strlen(TSI_TEST_UNUSED_BYTES));
  GPR_ASSERT(memcmp(bytes, TSI_TEST_UNUSED_BYTES, bytes_size) == 0);
  GPR_ASSERT(tsi_handshaker_result_get_unused_bytes(
                 result_without_unused_bytes, &bytes, &bytes_size) == TSI_OK);
  GPR_ASSERT(bytes_size == 0);
  GPR_ASSERT(bytes == nullptr);
}

static void check_handshake_results(tsi_test_fixture* fixture) {
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(fixture->vtable != nullptr);
  GPR_ASSERT(fixture->vtable->check_handshaker_peers != nullptr);
  // Check handshaker peers.
  fixture->vtable->check_handshaker_peers(fixture);
  // Check unused bytes.
  if (fixture->test_unused_bytes) {
    tsi_test_channel* channel = fixture->channel;
    if (fixture->server_result != nullptr &&
        fixture->client_result != nullptr) {
      check_unused_bytes(fixture);
    }
    channel->bytes_written_to_server_channel = 0;
    channel->bytes_written_to_client_channel = 0;
    channel->bytes_read_from_client_channel = 0;
    channel->bytes_read_from_server_channel = 0;
  }
}

static void send_bytes_to_peer(tsi_test_channel* test_channel,
                               const unsigned char* buf, size_t buf_size,
                               bool is_client) {
  GPR_ASSERT(test_channel != nullptr);
  GPR_ASSERT(buf != nullptr);
  uint8_t* channel =
      is_client ? test_channel->server_channel : test_channel->client_channel;
  GPR_ASSERT(channel != nullptr);
  size_t* bytes_written = is_client
                              ? &test_channel->bytes_written_to_server_channel
                              : &test_channel->bytes_written_to_client_channel;
  GPR_ASSERT(bytes_written != nullptr);
  GPR_ASSERT(*bytes_written + buf_size <= TSI_TEST_DEFAULT_CHANNEL_SIZE);
  // Write data to channel.
  memcpy(channel + *bytes_written, buf, buf_size);
  *bytes_written += buf_size;
}

static void maybe_append_unused_bytes(handshaker_args* args) {
  GPR_ASSERT(args != nullptr);
  GPR_ASSERT(args->fixture != nullptr);
  tsi_test_fixture* fixture = args->fixture;
  if (fixture->test_unused_bytes && !args->appended_unused_bytes) {
    args->appended_unused_bytes = true;
    send_bytes_to_peer(
        fixture->channel,
        reinterpret_cast<const unsigned char*>(TSI_TEST_UNUSED_BYTES),
        strlen(TSI_TEST_UNUSED_BYTES), args->is_client);
    if (fixture->client_result != nullptr &&
        fixture->server_result == nullptr) {
      fixture->has_client_finished_first = true;
    }
  }
}

static void receive_bytes_from_peer(tsi_test_channel* test_channel,
                                    unsigned char** buf, size_t* buf_size,
                                    bool is_client) {
  GPR_ASSERT(test_channel != nullptr);
  GPR_ASSERT(*buf != nullptr);
  GPR_ASSERT(buf_size != nullptr);
  uint8_t* channel =
      is_client ? test_channel->client_channel : test_channel->server_channel;
  GPR_ASSERT(channel != nullptr);
  size_t* bytes_read = is_client
                           ? &test_channel->bytes_read_from_client_channel
                           : &test_channel->bytes_read_from_server_channel;
  size_t* bytes_written = is_client
                              ? &test_channel->bytes_written_to_client_channel
                              : &test_channel->bytes_written_to_server_channel;
  GPR_ASSERT(bytes_read != nullptr);
  GPR_ASSERT(bytes_written != nullptr);
  size_t to_read = *buf_size < *bytes_written - *bytes_read
                       ? *buf_size
                       : *bytes_written - *bytes_read;
  // Read data from channel.
  memcpy(*buf, channel + *bytes_read, to_read);
  *buf_size = to_read;
  *bytes_read += to_read;
}

void tsi_test_frame_protector_send_message_to_peer(
    tsi_test_frame_protector_config* config, tsi_test_channel* channel,
    tsi_frame_protector* protector, bool is_client) {
  // Initialization.
  GPR_ASSERT(config != nullptr);
  GPR_ASSERT(channel != nullptr);
  GPR_ASSERT(protector != nullptr);
  unsigned char* protected_buffer =
      static_cast<unsigned char*>(gpr_zalloc(config->protected_buffer_size));
  size_t message_size =
      is_client ? config->client_message_size : config->server_message_size;
  uint8_t* message =
      is_client ? config->client_message : config->server_message;
  GPR_ASSERT(message != nullptr);
  const unsigned char* message_bytes =
      reinterpret_cast<unsigned char*>(message);
  tsi_result result = TSI_OK;
  // Do protect and send protected data to peer.
  while (message_size > 0 && result == TSI_OK) {
    size_t protected_buffer_size_to_send = config->protected_buffer_size;
    size_t processed_message_size = message_size;
    // Do protect.
    result = tsi_frame_protector_protect(
        protector, message_bytes, &processed_message_size, protected_buffer,
        &protected_buffer_size_to_send);
    GPR_ASSERT(result == TSI_OK);
    // Send protected data to peer.
    send_bytes_to_peer(channel, protected_buffer, protected_buffer_size_to_send,
                       is_client);
    message_bytes += processed_message_size;
    message_size -= processed_message_size;
    // Flush if we're done.
    if (message_size == 0) {
      size_t still_pending_size;
      do {
        protected_buffer_size_to_send = config->protected_buffer_size;
        result = tsi_frame_protector_protect_flush(
            protector, protected_buffer, &protected_buffer_size_to_send,
            &still_pending_size);
        GPR_ASSERT(result == TSI_OK);
        send_bytes_to_peer(channel, protected_buffer,
                           protected_buffer_size_to_send, is_client);
      } while (still_pending_size > 0 && result == TSI_OK);
      GPR_ASSERT(result == TSI_OK);
    }
  }
  GPR_ASSERT(result == TSI_OK);
  gpr_free(protected_buffer);
}

void tsi_test_frame_protector_receive_message_from_peer(
    tsi_test_frame_protector_config* config, tsi_test_channel* channel,
    tsi_frame_protector* protector, unsigned char* message,
    size_t* bytes_received, bool is_client) {
  // Initialization.
  GPR_ASSERT(config != nullptr);
  GPR_ASSERT(channel != nullptr);
  GPR_ASSERT(protector != nullptr);
  GPR_ASSERT(message != nullptr);
  GPR_ASSERT(bytes_received != nullptr);
  size_t read_offset = 0;
  size_t message_offset = 0;
  size_t read_from_peer_size = 0;
  tsi_result result = TSI_OK;
  bool done = false;
  unsigned char* read_buffer = static_cast<unsigned char*>(
      gpr_zalloc(config->read_buffer_allocated_size));
  unsigned char* message_buffer = static_cast<unsigned char*>(
      gpr_zalloc(config->message_buffer_allocated_size));
  // Do unprotect on data received from peer.
  while (!done && result == TSI_OK) {
    // Receive data from peer.
    if (read_from_peer_size == 0) {
      read_from_peer_size = config->read_buffer_allocated_size;
      receive_bytes_from_peer(channel, &read_buffer, &read_from_peer_size,
                              is_client);
      read_offset = 0;
    }
    if (read_from_peer_size == 0) {
      done = true;
    }
    // Do unprotect.
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

grpc_error_handle on_handshake_next_done(
    tsi_result result, void* user_data, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  handshaker_args* args = static_cast<handshaker_args*>(user_data);
  GPR_ASSERT(args != nullptr);
  GPR_ASSERT(args->fixture != nullptr);
  tsi_test_fixture* fixture = args->fixture;
  grpc_error_handle error;
  // Read more data if we need to.
  if (result == TSI_INCOMPLETE_DATA) {
    GPR_ASSERT(bytes_to_send_size == 0);
    notification_signal(fixture);
    return error;
  }
  if (result != TSI_OK) {
    notification_signal(fixture);
    return grpc_set_tsi_error_result(GRPC_ERROR_CREATE("Handshake failed"),
                                     result);
  }
  // Update handshaker result.
  if (handshaker_result != nullptr) {
    tsi_handshaker_result** result_to_write =
        args->is_client ? &fixture->client_result : &fixture->server_result;
    GPR_ASSERT(*result_to_write == nullptr);
    *result_to_write = handshaker_result;
  }
  // Send data to peer, if needed.
  if (bytes_to_send_size > 0) {
    send_bytes_to_peer(fixture->channel, bytes_to_send, bytes_to_send_size,
                       args->is_client);
    args->transferred_data = true;
  }
  if (handshaker_result != nullptr) {
    maybe_append_unused_bytes(args);
  }
  notification_signal(fixture);
  return error;
}

static void on_handshake_next_done_wrapper(
    tsi_result result, void* user_data, const unsigned char* bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result* handshaker_result) {
  handshaker_args* args = static_cast<handshaker_args*>(user_data);
  args->error = on_handshake_next_done(result, user_data, bytes_to_send,
                                       bytes_to_send_size, handshaker_result);
}

static bool is_handshake_finished_properly(handshaker_args* args) {
  GPR_ASSERT(args != nullptr);
  GPR_ASSERT(args->fixture != nullptr);
  tsi_test_fixture* fixture = args->fixture;
  return (args->is_client && fixture->client_result != nullptr) ||
         (!args->is_client && fixture->server_result != nullptr);
}

static void do_handshaker_next(handshaker_args* args) {
  // Initialization.
  GPR_ASSERT(args != nullptr);
  GPR_ASSERT(args->fixture != nullptr);
  tsi_test_fixture* fixture = args->fixture;
  tsi_handshaker* handshaker =
      args->is_client ? fixture->client_handshaker : fixture->server_handshaker;
  if (is_handshake_finished_properly(args)) {
    return;
  }
  tsi_handshaker_result* handshaker_result = nullptr;
  unsigned char* bytes_to_send = nullptr;
  size_t bytes_to_send_size = 0;
  tsi_result result = TSI_OK;
  // Receive data from peer, if available.
  do {
    size_t buf_size = args->handshake_buffer_size;
    receive_bytes_from_peer(fixture->channel, &args->handshake_buffer,
                            &buf_size, args->is_client);
    if (buf_size > 0) {
      args->transferred_data = true;
    }
    // Peform handshaker next.
    result = tsi_handshaker_next(
        handshaker, args->handshake_buffer, buf_size,
        const_cast<const unsigned char**>(&bytes_to_send), &bytes_to_send_size,
        &handshaker_result, &on_handshake_next_done_wrapper, args);
    if (result != TSI_ASYNC) {
      args->error = on_handshake_next_done(
          result, args, bytes_to_send, bytes_to_send_size, handshaker_result);
      if (!args->error.ok()) {
        return;
      }
    }
  } while (result == TSI_INCOMPLETE_DATA);
  notification_wait(fixture);
}

void tsi_test_do_handshake(tsi_test_fixture* fixture) {
  // Initializaiton.
  setup_handshakers(fixture);
  handshaker_args* client_args =
      handshaker_args_create(fixture, true /* is_client */);
  handshaker_args* server_args =
      handshaker_args_create(fixture, false /* is_client */);
  // Do handshake.
  do {
    client_args->transferred_data = false;
    server_args->transferred_data = false;
    do_handshaker_next(client_args);
    if (!client_args->error.ok()) {
      break;
    }
    do_handshaker_next(server_args);
    if (!server_args->error.ok()) {
      break;
    }
    // If this assertion is hit, this is likely an indication that the client
    // and server handshakers are hanging, each thinking that the other is
    // responsible for sending the next chunk of bytes to the other. This can
    // happen e.g. when a bug in the handshaker code results in some bytes being
    // dropped instead of passed to the BIO or SSL objects.
    GPR_ASSERT(client_args->transferred_data || server_args->transferred_data);
  } while (fixture->client_result == nullptr ||
           fixture->server_result == nullptr);
  // Verify handshake results.
  check_handshake_results(fixture);
  // Cleanup.
  handshaker_args_destroy(client_args);
  handshaker_args_destroy(server_args);
}

static void tsi_test_do_ping_pong(tsi_test_frame_protector_config* config,
                                  tsi_test_channel* channel,
                                  tsi_frame_protector* client_frame_protector,
                                  tsi_frame_protector* server_frame_protector) {
  GPR_ASSERT(config != nullptr);
  GPR_ASSERT(channel != nullptr);
  GPR_ASSERT(client_frame_protector != nullptr);
  GPR_ASSERT(server_frame_protector != nullptr);
  // Client sends a message to server.
  tsi_test_frame_protector_send_message_to_peer(
      config, channel, client_frame_protector, true /* is_client */);
  unsigned char* server_received_message =
      static_cast<unsigned char*>(gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  size_t server_received_message_size = 0;
  tsi_test_frame_protector_receive_message_from_peer(
      config, channel, server_frame_protector, server_received_message,
      &server_received_message_size, false /* is_client */);
  GPR_ASSERT(config->client_message_size == server_received_message_size);
  GPR_ASSERT(memcmp(config->client_message, server_received_message,
                    server_received_message_size) == 0);
  // Server sends a message to client.
  tsi_test_frame_protector_send_message_to_peer(
      config, channel, server_frame_protector, false /* is_client */);
  unsigned char* client_received_message =
      static_cast<unsigned char*>(gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  size_t client_received_message_size = 0;
  tsi_test_frame_protector_receive_message_from_peer(
      config, channel, client_frame_protector, client_received_message,
      &client_received_message_size, true /* is_client */);
  GPR_ASSERT(config->server_message_size == client_received_message_size);
  GPR_ASSERT(memcmp(config->server_message, client_received_message,
                    client_received_message_size) == 0);
  gpr_free(server_received_message);
  gpr_free(client_received_message);
}

void tsi_test_frame_protector_do_round_trip_no_handshake(
    tsi_test_frame_protector_fixture* fixture) {
  GPR_ASSERT(fixture != nullptr);
  tsi_test_do_ping_pong(fixture->config, fixture->channel,
                        fixture->client_frame_protector,
                        fixture->server_frame_protector);
}

void tsi_test_do_round_trip(tsi_test_fixture* fixture) {
  // Initialization.
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(fixture->config != nullptr);
  tsi_test_frame_protector_config* config = fixture->config;
  tsi_frame_protector* client_frame_protector = nullptr;
  tsi_frame_protector* server_frame_protector = nullptr;
  // Perform handshake.
  tsi_test_do_handshake(fixture);
  // Create frame protectors.
  size_t client_max_output_protected_frame_size =
      config->client_max_output_protected_frame_size;
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 fixture->client_result,
                 client_max_output_protected_frame_size == 0
                     ? nullptr
                     : &client_max_output_protected_frame_size,
                 &client_frame_protector) == TSI_OK);
  size_t server_max_output_protected_frame_size =
      config->server_max_output_protected_frame_size;
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 fixture->server_result,
                 server_max_output_protected_frame_size == 0
                     ? nullptr
                     : &server_max_output_protected_frame_size,
                 &server_frame_protector) == TSI_OK);
  tsi_test_do_ping_pong(config, fixture->channel, client_frame_protector,
                        server_frame_protector);
  // Destroy server and client frame protectors.
  tsi_frame_protector_destroy(client_frame_protector);
  tsi_frame_protector_destroy(server_frame_protector);
}

static unsigned char* generate_random_message(size_t size) {
  size_t i;
  unsigned char chars[] = "abcdefghijklmnopqrstuvwxyz1234567890";
  unsigned char* output =
      static_cast<unsigned char*>(gpr_zalloc(sizeof(unsigned char) * size));
  for (i = 0; i < size - 1; ++i) {
    output[i] = chars[rand() % static_cast<int>(sizeof(chars) - 1)];
  }
  return output;
}

tsi_test_frame_protector_config* tsi_test_frame_protector_config_create(
    bool use_default_read_buffer_allocated_size,
    bool use_default_message_buffer_allocated_size,
    bool use_default_protected_buffer_size, bool use_default_client_message,
    bool use_default_server_message,
    bool use_default_client_max_output_protected_frame_size,
    bool use_default_server_max_output_protected_frame_size) {
  tsi_test_frame_protector_config* config =
      static_cast<tsi_test_frame_protector_config*>(
          gpr_zalloc(sizeof(*config)));
  // Set the value for read_buffer_allocated_size.
  config->read_buffer_allocated_size =
      use_default_read_buffer_allocated_size
          ? TSI_TEST_DEFAULT_BUFFER_SIZE
          : TSI_TEST_SMALL_READ_BUFFER_ALLOCATED_SIZE;
  // Set the value for message_buffer_allocated_size.
  config->message_buffer_allocated_size =
      use_default_message_buffer_allocated_size
          ? TSI_TEST_DEFAULT_BUFFER_SIZE
          : TSI_TEST_SMALL_MESSAGE_BUFFER_ALLOCATED_SIZE;
  // Set the value for protected_buffer_size.
  config->protected_buffer_size = use_default_protected_buffer_size
                                      ? TSI_TEST_DEFAULT_PROTECTED_BUFFER_SIZE
                                      : TSI_TEST_SMALL_PROTECTED_BUFFER_SIZE;
  // Set the value for client message.
  config->client_message_size = use_default_client_message
                                    ? TSI_TEST_BIG_MESSAGE_SIZE
                                    : TSI_TEST_SMALL_MESSAGE_SIZE;
  config->client_message =
      use_default_client_message
          ? generate_random_message(TSI_TEST_BIG_MESSAGE_SIZE)
          : generate_random_message(TSI_TEST_SMALL_MESSAGE_SIZE);
  // Set the value for server message.
  config->server_message_size = use_default_server_message
                                    ? TSI_TEST_BIG_MESSAGE_SIZE
                                    : TSI_TEST_SMALL_MESSAGE_SIZE;
  config->server_message =
      use_default_server_message
          ? generate_random_message(TSI_TEST_BIG_MESSAGE_SIZE)
          : generate_random_message(TSI_TEST_SMALL_MESSAGE_SIZE);
  // Set the value for client max_output_protected_frame_size.
  // If it is 0, we pass NULL to tsi_handshaker_result_create_frame_protector(),
  // which then uses default protected frame size for it.
  config->client_max_output_protected_frame_size =
      use_default_client_max_output_protected_frame_size
          ? 0
          : TSI_TEST_SMALL_CLIENT_MAX_OUTPUT_PROTECTED_FRAME_SIZE;
  // Set the value for server max_output_protected_frame_size.
  // If it is 0, we pass NULL to tsi_handshaker_result_create_frame_protector(),
  // which then uses default protected frame size for it.
  config->server_max_output_protected_frame_size =
      use_default_server_max_output_protected_frame_size
          ? 0
          : TSI_TEST_SMALL_SERVER_MAX_OUTPUT_PROTECTED_FRAME_SIZE;
  return config;
}

void tsi_test_frame_protector_config_set_buffer_size(
    tsi_test_frame_protector_config* config, size_t read_buffer_allocated_size,
    size_t message_buffer_allocated_size, size_t protected_buffer_size,
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

void tsi_test_frame_protector_config_destroy(
    tsi_test_frame_protector_config* config) {
  if (config == nullptr) {
    return;
  }
  gpr_free(config->client_message);
  gpr_free(config->server_message);
  gpr_free(config);
}

static tsi_test_channel* tsi_test_channel_create() {
  tsi_test_channel* channel = grpc_core::Zalloc<tsi_test_channel>();
  channel->client_channel =
      static_cast<uint8_t*>(gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  channel->server_channel =
      static_cast<uint8_t*>(gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  channel->bytes_written_to_client_channel = 0;
  channel->bytes_written_to_server_channel = 0;
  channel->bytes_read_from_client_channel = 0;
  channel->bytes_read_from_server_channel = 0;
  return channel;
}

static void tsi_test_channel_destroy(tsi_test_channel* channel) {
  if (channel == nullptr) {
    return;
  }
  gpr_free(channel->client_channel);
  gpr_free(channel->server_channel);
  gpr_free(channel);
}

void tsi_test_fixture_init(tsi_test_fixture* fixture) {
  memset(fixture, 0, sizeof(tsi_test_fixture));
  fixture->config = tsi_test_frame_protector_config_create(
      true, true, true, true, true, true, true);
  fixture->handshake_buffer_size = TSI_TEST_DEFAULT_BUFFER_SIZE;
  fixture->channel = tsi_test_channel_create();
  fixture->test_unused_bytes = true;
  fixture->has_client_finished_first = false;
  gpr_mu_init(&fixture->mu);
  gpr_cv_init(&fixture->cv);
  fixture->notified = false;
}

void tsi_test_fixture_destroy(tsi_test_fixture* fixture) {
  if (fixture == nullptr) {
    return;
  }
  tsi_test_frame_protector_config_destroy(fixture->config);
  tsi_handshaker_destroy(fixture->client_handshaker);
  tsi_handshaker_destroy(fixture->server_handshaker);
  tsi_handshaker_result_destroy(fixture->client_result);
  tsi_handshaker_result_destroy(fixture->server_result);
  tsi_test_channel_destroy(fixture->channel);
  GPR_ASSERT(fixture->vtable != nullptr);
  GPR_ASSERT(fixture->vtable->destruct != nullptr);
  gpr_mu_destroy(&fixture->mu);
  gpr_cv_destroy(&fixture->cv);
  fixture->vtable->destruct(fixture);
}

tsi_test_frame_protector_fixture* tsi_test_frame_protector_fixture_create() {
  tsi_test_frame_protector_fixture* fixture =
      static_cast<tsi_test_frame_protector_fixture*>(
          gpr_zalloc(sizeof(*fixture)));
  fixture->config = tsi_test_frame_protector_config_create(
      true, true, true, true, true, true, true);
  fixture->channel = tsi_test_channel_create();
  return fixture;
}

void tsi_test_frame_protector_fixture_init(
    tsi_test_frame_protector_fixture* fixture,
    tsi_frame_protector* client_frame_protector,
    tsi_frame_protector* server_frame_protector) {
  GPR_ASSERT(fixture != nullptr);
  fixture->client_frame_protector = client_frame_protector;
  fixture->server_frame_protector = server_frame_protector;
}

void tsi_test_frame_protector_fixture_destroy(
    tsi_test_frame_protector_fixture* fixture) {
  if (fixture == nullptr) {
    return;
  }
  tsi_test_frame_protector_config_destroy(fixture->config);
  tsi_test_channel_destroy(fixture->channel);
  tsi_frame_protector_destroy(fixture->client_frame_protector);
  tsi_frame_protector_destroy(fixture->server_frame_protector);
  gpr_free(fixture);
}

std::string GenerateSelfSignedCertificate(
    const SelfSignedCertificateOptions& options) {
  // Generate an RSA keypair.
  RSA* rsa = RSA_new();
  BIGNUM* bignum = BN_new();
  GPR_ASSERT(BN_set_word(bignum, RSA_F4));
  GPR_ASSERT(
      RSA_generate_key_ex(rsa, /*key_size=*/2048, bignum, /*cb=*/nullptr));
  EVP_PKEY* key = EVP_PKEY_new();
  GPR_ASSERT(EVP_PKEY_assign_RSA(key, rsa));

  // Create the X509 object.
  X509* x509 = X509_new();
  GPR_ASSERT(X509_set_version(x509, X509_VERSION_3));

  // Set the not_before/after fields to infinite past/future. The value for
  // infinite future is from RFC 5280 Section 4.1.2.5.1.
  ASN1_UTCTIME* infinite_past = ASN1_UTCTIME_new();
  GPR_ASSERT(ASN1_UTCTIME_set(infinite_past, /*posix_time=*/0));
  GPR_ASSERT(X509_set1_notBefore(x509, infinite_past));
  ASN1_UTCTIME_free(infinite_past);
  ASN1_GENERALIZEDTIME* infinite_future = ASN1_GENERALIZEDTIME_new();
  GPR_ASSERT(
      ASN1_GENERALIZEDTIME_set_string(infinite_future, "99991231235959Z"));
  GPR_ASSERT(X509_set1_notAfter(x509, infinite_future));
  ASN1_GENERALIZEDTIME_free(infinite_future);

  // Set the subject DN.
  X509_NAME* subject_name = X509_NAME_new();
  GPR_ASSERT(X509_NAME_add_entry_by_txt(
      subject_name, "CN", MBSTRING_ASC,
      reinterpret_cast<const unsigned char*>(options.common_name.c_str()),
      /*len=*/-1, /*loc=*/-1,
      /*set=*/0));
  GPR_ASSERT(X509_NAME_add_entry_by_txt(
      subject_name, "O", MBSTRING_ASC,
      reinterpret_cast<const unsigned char*>(options.organization.c_str()),
      /*len=*/-1, /*loc=*/-1,
      /*set=*/0));
  GPR_ASSERT(
      X509_NAME_add_entry_by_txt(subject_name, "OU", MBSTRING_ASC,
                                 reinterpret_cast<const unsigned char*>(
                                     options.organizational_unit.c_str()),
                                 /*len=*/-1, /*loc=*/-1,
                                 /*set=*/0));
  GPR_ASSERT(X509_set_subject_name(x509, subject_name));
  X509_NAME_free(subject_name);

  // Set the public key and sign the certificate.
  GPR_ASSERT(X509_set_pubkey(x509, key));
  GPR_ASSERT(X509_sign(x509, key, EVP_sha256()));

  // Convert to PEM.
  BIO* bio = BIO_new(BIO_s_mem());
  GPR_ASSERT(PEM_write_bio_X509(bio, x509));
  const uint8_t* data = nullptr;
  size_t len = 0;
  GPR_ASSERT(BIO_mem_contents(bio, &data, &len));
  std::string pem = std::string(reinterpret_cast<const char*>(data), len);

  // Cleanup all of the OpenSSL objects and return the PEM-encoded cert.
  EVP_PKEY_free(key);
  X509_free(x509);
  BIO_free(bio);
  BN_free(bignum);
  return pem;
}
