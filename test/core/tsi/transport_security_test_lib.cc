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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include "src/core/lib/security/transport/tsi_error.h"
#include "test/core/tsi/transport_security_test_lib.h"

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
  grpc_error* error;
} handshaker_args;

static handshaker_args* handshaker_args_create(tsi_test_fixture* fixture,
                                               bool is_client) {
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(fixture->config != nullptr);
  handshaker_args* args =
      static_cast<handshaker_args*>(gpr_zalloc(sizeof(*args)));
  args->fixture = fixture;
  args->handshake_buffer_size = fixture->handshake_buffer_size;
  args->handshake_buffer =
      static_cast<unsigned char*>(gpr_zalloc(args->handshake_buffer_size));
  args->is_client = is_client;
  args->error = GRPC_ERROR_NONE;
  return args;
}

static void handshaker_args_destroy(handshaker_args* args) {
  gpr_free(args->handshake_buffer);
  GRPC_ERROR_UNREF(args->error);
  gpr_free(args);
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
  /* Check handshaker peers. */
  fixture->vtable->check_handshaker_peers(fixture);
  /* Check unused bytes. */
  if (fixture->test_unused_bytes) {
    if (fixture->server_result != nullptr &&
        fixture->client_result != nullptr) {
      check_unused_bytes(fixture);
    }
    fixture->bytes_written_to_server_channel = 0;
    fixture->bytes_written_to_client_channel = 0;
    fixture->bytes_read_from_client_channel = 0;
    fixture->bytes_read_from_server_channel = 0;
  }
}

static void send_bytes_to_peer(tsi_test_fixture* fixture,
                               const unsigned char* buf, size_t buf_size,
                               bool is_client) {
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(buf != nullptr);
  uint8_t* channel =
      is_client ? fixture->server_channel : fixture->client_channel;
  GPR_ASSERT(channel != nullptr);
  size_t* bytes_written = is_client ? &fixture->bytes_written_to_server_channel
                                    : &fixture->bytes_written_to_client_channel;
  GPR_ASSERT(bytes_written != nullptr);
  GPR_ASSERT(*bytes_written + buf_size <= TSI_TEST_DEFAULT_CHANNEL_SIZE);
  /* Write data to channel. */
  memcpy(channel + *bytes_written, buf, buf_size);
  *bytes_written += buf_size;
}

static void maybe_append_unused_bytes(handshaker_args* args) {
  GPR_ASSERT(args != nullptr);
  GPR_ASSERT(args->fixture != nullptr);
  tsi_test_fixture* fixture = args->fixture;
  if (fixture->test_unused_bytes && !args->appended_unused_bytes) {
    args->appended_unused_bytes = true;
    send_bytes_to_peer(fixture, (const unsigned char*)TSI_TEST_UNUSED_BYTES,
                       strlen(TSI_TEST_UNUSED_BYTES), args->is_client);
    if (fixture->client_result != nullptr &&
        fixture->server_result == nullptr) {
      fixture->has_client_finished_first = true;
    }
  }
}

static void receive_bytes_from_peer(tsi_test_fixture* fixture,
                                    unsigned char** buf, size_t* buf_size,
                                    bool is_client) {
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(*buf != nullptr);
  GPR_ASSERT(buf_size != nullptr);
  uint8_t* channel =
      is_client ? fixture->client_channel : fixture->server_channel;
  GPR_ASSERT(channel != nullptr);
  size_t* bytes_read = is_client ? &fixture->bytes_read_from_client_channel
                                 : &fixture->bytes_read_from_server_channel;
  size_t* bytes_written = is_client ? &fixture->bytes_written_to_client_channel
                                    : &fixture->bytes_written_to_server_channel;
  GPR_ASSERT(bytes_read != nullptr);
  GPR_ASSERT(bytes_written != nullptr);
  size_t to_read = *buf_size < *bytes_written - *bytes_read
                       ? *buf_size
                       : *bytes_written - *bytes_read;
  /* Read data from channel. */
  memcpy(*buf, channel + *bytes_read, to_read);
  *buf_size = to_read;
  *bytes_read += to_read;
}

static void send_message_to_peer(tsi_test_fixture* fixture,
                                 tsi_frame_protector* protector,
                                 bool is_client) {
  /* Initialization. */
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(fixture->config != nullptr);
  GPR_ASSERT(protector != nullptr);
  tsi_test_frame_protector_config* config = fixture->config;
  unsigned char* protected_buffer =
      static_cast<unsigned char*>(gpr_zalloc(config->protected_buffer_size));
  size_t message_size =
      is_client ? config->client_message_size : config->server_message_size;
  uint8_t* message =
      is_client ? config->client_message : config->server_message;
  GPR_ASSERT(message != nullptr);
  const unsigned char* message_bytes = (const unsigned char*)message;
  tsi_result result = TSI_OK;
  /* Do protect and send protected data to peer. */
  while (message_size > 0 && result == TSI_OK) {
    size_t protected_buffer_size_to_send = config->protected_buffer_size;
    size_t processed_message_size = message_size;
    /* Do protect. */
    result = tsi_frame_protector_protect(
        protector, message_bytes, &processed_message_size, protected_buffer,
        &protected_buffer_size_to_send);
    GPR_ASSERT(result == TSI_OK);
    /* Send protected data to peer. */
    send_bytes_to_peer(fixture, protected_buffer, protected_buffer_size_to_send,
                       is_client);
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
        send_bytes_to_peer(fixture, protected_buffer,
                           protected_buffer_size_to_send, is_client);
      } while (still_pending_size > 0 && result == TSI_OK);
      GPR_ASSERT(result == TSI_OK);
    }
  }
  GPR_ASSERT(result == TSI_OK);
  gpr_free(protected_buffer);
}

static void receive_message_from_peer(tsi_test_fixture* fixture,
                                      tsi_frame_protector* protector,
                                      unsigned char* message,
                                      size_t* bytes_received, bool is_client) {
  /* Initialization. */
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(protector != nullptr);
  GPR_ASSERT(message != nullptr);
  GPR_ASSERT(bytes_received != nullptr);
  GPR_ASSERT(fixture->config != nullptr);
  tsi_test_frame_protector_config* config = fixture->config;
  size_t read_offset = 0;
  size_t message_offset = 0;
  size_t read_from_peer_size = 0;
  tsi_result result = TSI_OK;
  bool done = false;
  unsigned char* read_buffer = static_cast<unsigned char*>(
      gpr_zalloc(config->read_buffer_allocated_size));
  unsigned char* message_buffer = static_cast<unsigned char*>(
      gpr_zalloc(config->message_buffer_allocated_size));
  /* Do unprotect on data received from peer. */
  while (!done && result == TSI_OK) {
    /* Receive data from peer. */
    if (read_from_peer_size == 0) {
      read_from_peer_size = config->read_buffer_allocated_size;
      receive_bytes_from_peer(fixture, &read_buffer, &read_from_peer_size,
                              is_client);
      read_offset = 0;
    }
    if (read_from_peer_size == 0) {
      done = true;
    }
    /* Do unprotect. */
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

grpc_error* on_handshake_next_done(tsi_result result, void* user_data,
                                   const unsigned char* bytes_to_send,
                                   size_t bytes_to_send_size,
                                   tsi_handshaker_result* handshaker_result) {
  handshaker_args* args = (handshaker_args*)user_data;
  GPR_ASSERT(args != nullptr);
  GPR_ASSERT(args->fixture != nullptr);
  tsi_test_fixture* fixture = args->fixture;
  grpc_error* error = GRPC_ERROR_NONE;
  /* Read more data if we need to. */
  if (result == TSI_INCOMPLETE_DATA) {
    GPR_ASSERT(bytes_to_send_size == 0);
    notification_signal(fixture);
    return error;
  }
  if (result != TSI_OK) {
    notification_signal(fixture);
    return grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshake failed"), result);
  }
  /* Update handshaker result. */
  if (handshaker_result != nullptr) {
    tsi_handshaker_result** result_to_write =
        args->is_client ? &fixture->client_result : &fixture->server_result;
    GPR_ASSERT(*result_to_write == nullptr);
    *result_to_write = handshaker_result;
  }
  /* Send data to peer, if needed. */
  if (bytes_to_send_size > 0) {
    send_bytes_to_peer(args->fixture, bytes_to_send, bytes_to_send_size,
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
  handshaker_args* args = (handshaker_args*)user_data;
  args->error = on_handshake_next_done(result, user_data, bytes_to_send,
                                       bytes_to_send_size, handshaker_result);
}

static bool is_handshake_finished_properly(handshaker_args* args) {
  GPR_ASSERT(args != nullptr);
  GPR_ASSERT(args->fixture != nullptr);
  tsi_test_fixture* fixture = args->fixture;
  if ((args->is_client && fixture->client_result != nullptr) ||
      (!args->is_client && fixture->server_result != nullptr)) {
    return true;
  }
  return false;
}

static void do_handshaker_next(handshaker_args* args) {
  /* Initialization. */
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
  /* Receive data from peer, if available. */
  do {
    size_t buf_size = args->handshake_buffer_size;
    receive_bytes_from_peer(args->fixture, &args->handshake_buffer, &buf_size,
                            args->is_client);
    if (buf_size > 0) {
      args->transferred_data = true;
    }
    /* Peform handshaker next. */
    result = tsi_handshaker_next(handshaker, args->handshake_buffer, buf_size,
                                 (const unsigned char**)&bytes_to_send,
                                 &bytes_to_send_size, &handshaker_result,
                                 &on_handshake_next_done_wrapper, args);
    if (result != TSI_ASYNC) {
      args->error = on_handshake_next_done(
          result, args, bytes_to_send, bytes_to_send_size, handshaker_result);
      if (args->error != GRPC_ERROR_NONE) {
        return;
      }
    }
  } while (result == TSI_INCOMPLETE_DATA);
  notification_wait(fixture);
}

void tsi_test_do_handshake(tsi_test_fixture* fixture) {
  /* Initializaiton. */
  setup_handshakers(fixture);
  handshaker_args* client_args =
      handshaker_args_create(fixture, true /* is_client */);
  handshaker_args* server_args =
      handshaker_args_create(fixture, false /* is_client */);
  /* Do handshake. */
  do {
    client_args->transferred_data = false;
    server_args->transferred_data = false;
    do_handshaker_next(client_args);
    if (client_args->error != GRPC_ERROR_NONE) {
      break;
    }
    do_handshaker_next(server_args);
    if (server_args->error != GRPC_ERROR_NONE) {
      break;
    }
    GPR_ASSERT(client_args->transferred_data || server_args->transferred_data);
  } while (fixture->client_result == nullptr ||
           fixture->server_result == nullptr);
  /* Verify handshake results. */
  check_handshake_results(fixture);
  /* Cleanup. */
  handshaker_args_destroy(client_args);
  handshaker_args_destroy(server_args);
}

void tsi_test_do_round_trip(tsi_test_fixture* fixture) {
  /* Initialization. */
  GPR_ASSERT(fixture != nullptr);
  GPR_ASSERT(fixture->config != nullptr);
  tsi_test_frame_protector_config* config = fixture->config;
  tsi_frame_protector* client_frame_protector = nullptr;
  tsi_frame_protector* server_frame_protector = nullptr;
  /* Perform handshake. */
  tsi_test_do_handshake(fixture);
  /* Create frame protectors.*/
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
  /* Client sends a message to server. */
  send_message_to_peer(fixture, client_frame_protector, true /* is_client */);
  unsigned char* server_received_message =
      static_cast<unsigned char*>(gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  size_t server_received_message_size = 0;
  receive_message_from_peer(
      fixture, server_frame_protector, server_received_message,
      &server_received_message_size, false /* is_client */);
  GPR_ASSERT(config->client_message_size == server_received_message_size);
  GPR_ASSERT(memcmp(config->client_message, server_received_message,
                    server_received_message_size) == 0);
  /* Server sends a message to client. */
  send_message_to_peer(fixture, server_frame_protector, false /* is_client */);
  unsigned char* client_received_message =
      static_cast<unsigned char*>(gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  size_t client_received_message_size = 0;
  receive_message_from_peer(
      fixture, client_frame_protector, client_received_message,
      &client_received_message_size, true /* is_client */);
  GPR_ASSERT(config->server_message_size == client_received_message_size);
  GPR_ASSERT(memcmp(config->server_message, client_received_message,
                    client_received_message_size) == 0);
  /* Destroy server and client frame protectors. */
  tsi_frame_protector_destroy(client_frame_protector);
  tsi_frame_protector_destroy(server_frame_protector);
  gpr_free(server_received_message);
  gpr_free(client_received_message);
}

static unsigned char* generate_random_message(size_t size) {
  size_t i;
  unsigned char chars[] = "abcdefghijklmnopqrstuvwxyz1234567890";
  unsigned char* output =
      static_cast<unsigned char*>(gpr_zalloc(sizeof(unsigned char) * size));
  for (i = 0; i < size - 1; ++i) {
    output[i] = chars[rand() % (int)(sizeof(chars) - 1)];
  }
  return output;
}

tsi_test_frame_protector_config* tsi_test_frame_protector_config_create(
    bool use_default_read_buffer_allocated_size,
    bool use_default_message_buffer_allocated_size,
    bool use_default_protected_buffer_size, bool use_default_client_message,
    bool use_default_server_message,
    bool use_default_client_max_output_protected_frame_size,
    bool use_default_server_max_output_protected_frame_size,
    bool use_default_handshake_buffer_size) {
  tsi_test_frame_protector_config* config =
      static_cast<tsi_test_frame_protector_config*>(
          gpr_zalloc(sizeof(*config)));
  /* Set the value for read_buffer_allocated_size. */
  config->read_buffer_allocated_size =
      use_default_read_buffer_allocated_size
          ? TSI_TEST_DEFAULT_BUFFER_SIZE
          : TSI_TEST_SMALL_READ_BUFFER_ALLOCATED_SIZE;
  /* Set the value for message_buffer_allocated_size. */
  config->message_buffer_allocated_size =
      use_default_message_buffer_allocated_size
          ? TSI_TEST_DEFAULT_BUFFER_SIZE
          : TSI_TEST_SMALL_MESSAGE_BUFFER_ALLOCATED_SIZE;
  /* Set the value for protected_buffer_size. */
  config->protected_buffer_size = use_default_protected_buffer_size
                                      ? TSI_TEST_DEFAULT_PROTECTED_BUFFER_SIZE
                                      : TSI_TEST_SMALL_PROTECTED_BUFFER_SIZE;
  /* Set the value for client message. */
  config->client_message_size = use_default_client_message
                                    ? TSI_TEST_BIG_MESSAGE_SIZE
                                    : TSI_TEST_SMALL_MESSAGE_SIZE;
  config->client_message =
      use_default_client_message
          ? generate_random_message(TSI_TEST_BIG_MESSAGE_SIZE)
          : generate_random_message(TSI_TEST_SMALL_MESSAGE_SIZE);
  /* Set the value for server message. */
  config->server_message_size = use_default_server_message
                                    ? TSI_TEST_BIG_MESSAGE_SIZE
                                    : TSI_TEST_SMALL_MESSAGE_SIZE;
  config->server_message =
      use_default_server_message
          ? generate_random_message(TSI_TEST_BIG_MESSAGE_SIZE)
          : generate_random_message(TSI_TEST_SMALL_MESSAGE_SIZE);
  /* Set the value for client max_output_protected_frame_size.
     If it is 0, we pass NULL to tsi_handshaker_result_create_frame_protector(),
     which then uses default protected frame size for it. */
  config->client_max_output_protected_frame_size =
      use_default_client_max_output_protected_frame_size
          ? 0
          : TSI_TEST_SMALL_CLIENT_MAX_OUTPUT_PROTECTED_FRAME_SIZE;
  /* Set the value for server max_output_protected_frame_size.
     If it is 0, we pass NULL to tsi_handshaker_result_create_frame_protector(),
     which then uses default protected frame size for it. */
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
  GPR_ASSERT(config != nullptr);
  gpr_free(config->client_message);
  gpr_free(config->server_message);
  gpr_free(config);
}

void tsi_test_fixture_init(tsi_test_fixture* fixture) {
  fixture->config = tsi_test_frame_protector_config_create(
      true, true, true, true, true, true, true, true);
  fixture->handshake_buffer_size = TSI_TEST_DEFAULT_BUFFER_SIZE;
  fixture->client_channel =
      static_cast<uint8_t*>(gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  fixture->server_channel =
      static_cast<uint8_t*>(gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE));
  fixture->bytes_written_to_client_channel = 0;
  fixture->bytes_written_to_server_channel = 0;
  fixture->bytes_read_from_client_channel = 0;
  fixture->bytes_read_from_server_channel = 0;
  fixture->test_unused_bytes = true;
  fixture->has_client_finished_first = false;
  gpr_mu_init(&fixture->mu);
  gpr_cv_init(&fixture->cv);
  fixture->notified = false;
}

void tsi_test_fixture_destroy(tsi_test_fixture* fixture) {
  GPR_ASSERT(fixture != nullptr);
  tsi_test_frame_protector_config_destroy(fixture->config);
  tsi_handshaker_destroy(fixture->client_handshaker);
  tsi_handshaker_destroy(fixture->server_handshaker);
  tsi_handshaker_result_destroy(fixture->client_result);
  tsi_handshaker_result_destroy(fixture->server_result);
  gpr_free(fixture->client_channel);
  gpr_free(fixture->server_channel);
  GPR_ASSERT(fixture->vtable != nullptr);
  GPR_ASSERT(fixture->vtable->destruct != nullptr);
  fixture->vtable->destruct(fixture);
  gpr_mu_destroy(&fixture->mu);
  gpr_cv_destroy(&fixture->cv);
  gpr_free(fixture);
}
