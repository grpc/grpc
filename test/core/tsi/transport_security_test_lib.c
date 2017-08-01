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
#include "src/core/lib/security/transport/tsi_error.h"
#include "test/core/tsi/transport_security_test_lib.h"

typedef struct handshaker_args {
  transport_security_test_lib *lib;
  unsigned char *handshake_buffer;
  size_t handshake_buffer_size;
  bool is_client;
  bool transferred_data;
  grpc_error *error;
} handshaker_args;

static handshaker_args *handshaker_args_create(transport_security_test_lib *lib,
                                               bool is_client) {
  GPR_ASSERT(lib != NULL && lib->config != NULL);
  handshaker_args *args = gpr_zalloc(sizeof(*args));
  args->lib = lib;
  args->handshake_buffer_size = lib->config->handshake_buffer_size;
  args->handshake_buffer = gpr_zalloc(args->handshake_buffer_size);
  args->is_client = is_client;
  args->error = GRPC_ERROR_NONE;
  return args;
}

static void handshaker_args_destroy(handshaker_args *args) {
  gpr_free(args->handshake_buffer);
  GRPC_ERROR_UNREF(args->error);
  gpr_free(args);
}

static void do_handshaker_next_locked(handshaker_args *args);

static void setup_handshakers(transport_security_test_lib *lib) {
  GPR_ASSERT(lib != NULL && lib->vtable != NULL &&
             lib->vtable->setup_handshakers != NULL);
  lib->vtable->setup_handshakers(lib);
}

static void check_handshake_results(transport_security_test_lib *lib) {
  GPR_ASSERT(lib != NULL && lib->vtable != NULL &&
             lib->vtable->check_handshake_results != NULL);
  lib->vtable->check_handshake_results(lib);
}

static void send_bytes_to_peer(transport_security_test_lib *lib,
                               const unsigned char *buf, size_t buf_size,
                               bool is_client) {
  GPR_ASSERT(lib != NULL && lib->config != NULL && buf != NULL);
  transport_security_test_config *config = lib->config;
  uint8_t *channel =
      is_client ? config->server_channel : config->client_channel;
  GPR_ASSERT(channel != NULL);
  size_t *bytes_written = is_client ? &config->bytes_written_to_server_channel
                                    : &config->bytes_written_to_client_channel;
  GPR_ASSERT(bytes_written != NULL);
  GPR_ASSERT(*bytes_written + buf_size <= TSI_TEST_DEFAULT_CHANNEL_SIZE);
  /* Write data to channel. */
  memcpy(channel + *bytes_written, buf, buf_size);
  *bytes_written += buf_size;
}

static void receive_bytes_from_peer(transport_security_test_lib *lib,
                                    unsigned char **buf, size_t *buf_size,
                                    bool is_client) {
  GPR_ASSERT(lib != NULL && lib->config != NULL && *buf != NULL &&
             buf_size != NULL);
  transport_security_test_config *config = lib->config;
  uint8_t *channel =
      is_client ? config->client_channel : config->server_channel;
  GPR_ASSERT(channel != NULL);
  size_t *bytes_read = is_client ? &config->bytes_read_from_client_channel
                                 : &config->bytes_read_from_server_channel;
  size_t *bytes_written = is_client ? &config->bytes_written_to_client_channel
                                    : &config->bytes_written_to_server_channel;
  GPR_ASSERT(bytes_read != NULL && bytes_written != NULL);
  size_t to_read = *buf_size < *bytes_written - *bytes_read
                       ? *buf_size
                       : *bytes_written - *bytes_read;
  /* Read data from channel. */
  memcpy(*buf, channel + *bytes_read, to_read);
  *buf_size = to_read;
  *bytes_read += to_read;
}

static void send_message_to_peer(transport_security_test_lib *lib,
                                 tsi_frame_protector *protector,
                                 bool is_client) {
  /* Initialization. */
  GPR_ASSERT(lib != NULL && lib->config != NULL && protector != NULL);
  transport_security_test_config *config = lib->config;
  unsigned char *protected_buffer = gpr_zalloc(config->protected_buffer_size);
  size_t message_size =
      is_client ? config->client_message_size : config->server_message_size;
  uint8_t *message =
      is_client ? config->client_message : config->server_message;
  GPR_ASSERT(message != NULL);
  const unsigned char *message_bytes = (const unsigned char *)message;
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
    send_bytes_to_peer(lib, protected_buffer, protected_buffer_size_to_send,
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
        send_bytes_to_peer(lib, protected_buffer, protected_buffer_size_to_send,
                           is_client);
      } while (still_pending_size > 0 && result == TSI_OK);
      GPR_ASSERT(result == TSI_OK);
    }
  }
  GPR_ASSERT(result == TSI_OK);
  gpr_free(protected_buffer);
}

static void receive_message_from_peer(transport_security_test_lib *lib,
                                      tsi_frame_protector *protector,
                                      unsigned char *message,
                                      size_t *bytes_received, bool is_client) {
  /* Initialization. */
  GPR_ASSERT(lib != NULL && protector != NULL && message != NULL &&
             bytes_received != NULL);
  transport_security_test_config *config = lib->config;
  GPR_ASSERT(config != NULL);
  size_t read_offset = 0;
  size_t message_offset = 0;
  size_t read_from_peer_size = 0;
  tsi_result result = TSI_OK;
  bool done = false;
  unsigned char *read_buffer = gpr_zalloc(config->read_buffer_allocated_size);
  unsigned char *message_buffer =
      gpr_zalloc(config->message_buffer_allocated_size);

  /* Do unprotect on data received from peer. */
  while (!done && result == TSI_OK) {
    /* Receive data from peer. */
    if (read_from_peer_size == 0) {
      read_from_peer_size = config->read_buffer_allocated_size;
      receive_bytes_from_peer(lib, &read_buffer, &read_from_peer_size,
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

grpc_error *on_handshake_next_done_locked(
    tsi_result result, void *user_data, const unsigned char *bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result *handshaker_result) {
  handshaker_args *args = (handshaker_args *)user_data;
  GPR_ASSERT(args != NULL && args->lib != NULL);
  transport_security_test_lib *lib = args->lib;
  grpc_error *error = GRPC_ERROR_NONE;
  /* Read more data if we need to. */
  if (result == TSI_INCOMPLETE_DATA) {
    GPR_ASSERT(bytes_to_send_size == 0);
    return error;
  }
  if (result != TSI_OK) {
    return grpc_set_tsi_error_result(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Handshake failed"), result);
  }
  /* Update handshaker result. */
  if (handshaker_result != NULL) {
    tsi_handshaker_result **result_to_write =
        args->is_client ? &lib->client_result : &lib->server_result;
    GPR_ASSERT(*result_to_write == NULL);
    *result_to_write = handshaker_result;
  }
  /* Send data to peer, if needed. */
  if (bytes_to_send_size > 0) {
    send_bytes_to_peer(args->lib, bytes_to_send, bytes_to_send_size,
                       args->is_client);
    args->transferred_data = true;
  }
  return error;
}

static void on_handshake_next_done_wrapper(
    tsi_result result, void *user_data, const unsigned char *bytes_to_send,
    size_t bytes_to_send_size, tsi_handshaker_result *handshaker_result) {
  handshaker_args *args = (handshaker_args *)user_data;
  args->error = on_handshake_next_done_locked(
      result, user_data, bytes_to_send, bytes_to_send_size, handshaker_result);
}

static bool is_handshake_finished_properly(handshaker_args *args) {
  GPR_ASSERT(args != NULL && args->lib != NULL);
  transport_security_test_lib *lib = args->lib;
  if ((args->is_client && lib->client_result != NULL) ||
      (!args->is_client && lib->server_result != NULL)) {
    return true;
  }
  return false;
}

static void do_handshaker_next_locked(handshaker_args *args) {
  /* Initialization. */
  GPR_ASSERT(args != NULL && args->lib != NULL);
  transport_security_test_lib *lib = args->lib;
  tsi_handshaker *handshaker =
      args->is_client ? lib->client_handshaker : lib->server_handshaker;
  if (is_handshake_finished_properly(args)) {
    return;
  }
  tsi_handshaker_result *handshaker_result = NULL;
  unsigned char *bytes_to_send = NULL;
  size_t bytes_to_send_size = 0;
  /* Receive data from peer, if available. */
  size_t buf_size = args->handshake_buffer_size;
  receive_bytes_from_peer(args->lib, &args->handshake_buffer, &buf_size,
                          args->is_client);
  if (buf_size > 0) {
    args->transferred_data = true;
  }
  /* Peform handshaker next. */
  tsi_result result = tsi_handshaker_next(
      handshaker, args->handshake_buffer, buf_size, &bytes_to_send,
      &bytes_to_send_size, &handshaker_result, &on_handshake_next_done_wrapper,
      args);
  if (result != TSI_ASYNC) {
    args->error = on_handshake_next_done_locked(
        result, args, bytes_to_send, bytes_to_send_size, handshaker_result);
  }
}

void transport_security_test_do_handshake(transport_security_test_lib *lib) {
  /* Initializaiton. */
  setup_handshakers(lib);
  handshaker_args *client_args =
      handshaker_args_create(lib, true /* is_client */);
  handshaker_args *server_args =
      handshaker_args_create(lib, false /* is_client */);

  /*Do handshake.*/
  do {
    client_args->transferred_data = false;
    server_args->transferred_data = false;
    do_handshaker_next_locked(client_args);
    if (client_args->error != GRPC_ERROR_NONE) {
      break;
    }
    do_handshaker_next_locked(server_args);
    if (server_args->error != GRPC_ERROR_NONE) {
      break;
    }
    GPR_ASSERT(client_args->transferred_data || server_args->transferred_data);
  } while (lib->client_result == NULL || lib->server_result == NULL);

  /* Verify handshake results. */
  check_handshake_results(lib);

  /* Cleanup. */
  handshaker_args_destroy(client_args);
  handshaker_args_destroy(server_args);
}

void transport_security_test_do_ping_pong(transport_security_test_lib *lib) {
  /* Initialization. */
  unsigned char to_server[TSI_TEST_DEFAULT_BUFFER_SIZE];
  unsigned char to_client[TSI_TEST_DEFAULT_BUFFER_SIZE];
  size_t max_frame_size = TSI_TEST_DEFAULT_BUFFER_SIZE;
  const char ping_request[] = "Ping";
  const char pong_response[] = "Pong";

  /* Perform handshake. */
  transport_security_test_do_handshake(lib);

  /* Create frame protectors. */
  tsi_frame_protector *client_frame_protector = NULL;
  tsi_frame_protector *server_frame_protector = NULL;
  GPR_ASSERT(lib->client_result != NULL && lib->server_result != NULL);
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 lib->client_result, &max_frame_size,
                 &client_frame_protector) == TSI_OK);
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 lib->server_result, &max_frame_size,
                 &server_frame_protector) == TSI_OK);

  /* Client sends a ping request. */
  size_t ping_length = strlen(ping_request);
  size_t protected_size = sizeof(to_server);
  GPR_ASSERT(tsi_frame_protector_protect(
                 client_frame_protector, (const unsigned char *)(ping_request),
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
                 server_frame_protector, (const unsigned char *)(pong_response),
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
}

void transport_security_test_do_round_trip(transport_security_test_lib *lib) {
  /* Initialization. */
  GPR_ASSERT(lib != NULL && lib->config != NULL);
  transport_security_test_config *config = lib->config;
  tsi_frame_protector *client_frame_protector = NULL;
  tsi_frame_protector *server_frame_protector = NULL;

  /* Perform handshake. */
  transport_security_test_do_handshake(lib);

  /* Create frame protectors.*/
  size_t client_max_output_protected_frame_size =
      config->client_max_output_protected_frame_size;
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 lib->client_result,
                 client_max_output_protected_frame_size == 0
                     ? NULL
                     : &client_max_output_protected_frame_size,
                 &client_frame_protector) == TSI_OK);
  size_t server_max_output_protected_frame_size =
      config->server_max_output_protected_frame_size;
  GPR_ASSERT(tsi_handshaker_result_create_frame_protector(
                 lib->server_result,
                 server_max_output_protected_frame_size == 0
                     ? NULL
                     : &server_max_output_protected_frame_size,
                 &server_frame_protector) == TSI_OK);

  /* Client sends a message to server. */
  send_message_to_peer(lib, client_frame_protector, true /* is_client */);
  unsigned char *server_received_message =
      gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE);
  size_t server_received_message_size = 0;
  receive_message_from_peer(
      lib, server_frame_protector, server_received_message,
      &server_received_message_size, false /* is_client */);
  GPR_ASSERT(config->client_message_size == server_received_message_size);
  GPR_ASSERT(memcmp(config->client_message, server_received_message,
                    server_received_message_size) == 0);

  /* Server sends a message to client. */
  send_message_to_peer(lib, server_frame_protector, false /* is_client */);
  unsigned char *client_received_message =
      gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE);
  size_t client_received_message_size = 0;
  receive_message_from_peer(
      lib, client_frame_protector, client_received_message,
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

static unsigned char *generate_random_message(size_t size) {
  size_t i;
  unsigned char chars[] = "abcdefghijklmnopqrstuvwxyz1234567890";
  unsigned char *output = gpr_zalloc(sizeof(unsigned char) * size);
  for (i = 0; i < size - 1; ++i) {
    output[i] = chars[rand() % (int)(sizeof(chars) - 1)];
  }
  output[size - 1] = '\0';
  return output;
}

transport_security_test_config *transport_security_test_config_create(
    bool use_default_read_buffer_allocated_size,
    bool use_default_message_buffer_allocated_size,
    bool use_default_protected_buffer_size, bool use_default_client_message,
    bool use_default_server_message,
    bool use_default_client_max_output_protected_frame_size,
    bool use_default_server_max_output_protected_frame_size,
    bool use_default_handshake_buffer_size) {
  transport_security_test_config *config = gpr_zalloc(sizeof(*config));
  config->small_message = generate_random_message(TSI_TEST_SMALL_MESSAGE_SIZE);
  config->big_message = generate_random_message(TSI_TEST_BIG_MESSAGE_SIZE);

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
      use_default_client_message ? config->big_message : config->small_message;

  /* Set the value for server message. */
  config->server_message_size = use_default_server_message
                                    ? TSI_TEST_BIG_MESSAGE_SIZE
                                    : TSI_TEST_SMALL_MESSAGE_SIZE;
  config->server_message =
      use_default_server_message ? config->big_message : config->small_message;

  /* Set the value for client max_output_protected_frame_size. */
  config->client_max_output_protected_frame_size =
      use_default_client_max_output_protected_frame_size
          ? 0
          : TSI_TEST_SMALL_CLIENT_MAX_OUTPUT_PROTECTED_FRAME_SIZE;

  /* Set the value for server max_output_protected_frame_size. */
  config->server_max_output_protected_frame_size =
      use_default_server_max_output_protected_frame_size
          ? 0
          : TSI_TEST_SMALL_SERVER_MAX_OUTPUT_PROTECTED_FRAME_SIZE;

  /* Set the value for handshake buffer size. */
  config->handshake_buffer_size = use_default_handshake_buffer_size
                                      ? TSI_TEST_DEFAULT_BUFFER_SIZE
                                      : TSI_TEST_SMALL_HANDSHAKE_BUFFER_SIZE;

  config->client_channel = gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE);
  config->server_channel = gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE);
  config->bytes_written_to_client_channel = 0;
  config->bytes_written_to_server_channel = 0;
  config->bytes_read_from_client_channel = 0;
  config->bytes_read_from_server_channel = 0;
  return config;
}

void transport_security_test_config_set_buffer_size(
    transport_security_test_config *config, size_t read_buffer_allocated_size,
    size_t message_buffer_allocated_size, size_t protected_buffer_size,
    size_t client_max_output_protected_frame_size,
    size_t server_max_output_protected_frame_size) {
  GPR_ASSERT(config != NULL);
  config->read_buffer_allocated_size = read_buffer_allocated_size;
  config->message_buffer_allocated_size = message_buffer_allocated_size;
  config->protected_buffer_size = protected_buffer_size;
  config->client_max_output_protected_frame_size =
      client_max_output_protected_frame_size;
  config->server_max_output_protected_frame_size =
      server_max_output_protected_frame_size;
}

void transport_security_test_config_destroy(
    transport_security_test_config *config) {
  gpr_free(config->client_channel);
  gpr_free(config->server_channel);
  gpr_free(config->big_message);
  gpr_free(config->small_message);
  gpr_free(config);
}

void transport_security_test_destroy(transport_security_test_lib *lib) {
  GPR_ASSERT(lib != NULL);
  GPR_ASSERT(lib->vtable != NULL && lib->vtable->destruct != NULL);
  lib->vtable->destruct(lib);
  gpr_free(lib);
}
