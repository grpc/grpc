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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "src/core/tsi/fake_transport_security.h"
#include "test/core/tsi/transport_security_test_lib.h"
#include "test/core/util/test_config.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

typedef struct fake_tsi_test_fixture {
  tsi_test_fixture base;
} fake_tsi_test_fixture;

static void fake_test_setup_handshakers(tsi_test_fixture *fixture) {
  fixture->client_handshaker = tsi_create_fake_handshaker(1 /* is_client. */);
  fixture->server_handshaker = tsi_create_fake_handshaker(0 /* is_client. */);
}

static void fake_test_check_handshake_results(tsi_test_fixture *fixture) {
  GPR_ASSERT(fixture->client_result != NULL);
  GPR_ASSERT(fixture->server_result != NULL);
  tsi_peer peer;
  GPR_ASSERT(tsi_handshaker_result_extract_peer(fixture->client_result,
                                                &peer) == TSI_OK);
  tsi_peer_destruct(&peer);
  GPR_ASSERT(tsi_handshaker_result_extract_peer(fixture->server_result,
                                                &peer) == TSI_OK);
  tsi_peer_destruct(&peer);
}

static void fake_test_destruct(tsi_test_fixture *fixture) {
  if (fixture == NULL) {
    return;
  }
  tsi_test_frame_protector_config_destroy(fixture->config);
  tsi_handshaker_destroy(fixture->client_handshaker);
  tsi_handshaker_destroy(fixture->server_handshaker);
  tsi_handshaker_result_destroy(fixture->client_result);
  tsi_handshaker_result_destroy(fixture->server_result);
  gpr_free(fixture->client_channel);
  gpr_free(fixture->server_channel);
}

static const struct tsi_test_fixture_vtable vtable = {
    fake_test_setup_handshakers, fake_test_check_handshake_results,
    fake_test_destruct};

static tsi_test_fixture *fake_tsi_test_fixture_create() {
  fake_tsi_test_fixture *fake_fixture = gpr_zalloc(sizeof(*fake_fixture));
  tsi_test_frame_protector_config *config =
      tsi_test_frame_protector_config_create(true, true, true, true, true, true,
                                             true, true);
  fake_fixture->base.config = config;
  fake_fixture->base.vtable = &vtable;
  fake_fixture->base.handshake_buffer_size = TSI_TEST_DEFAULT_BUFFER_SIZE;
  fake_fixture->base.client_channel = gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE);
  fake_fixture->base.server_channel = gpr_zalloc(TSI_TEST_DEFAULT_CHANNEL_SIZE);
  fake_fixture->base.bytes_written_to_client_channel = 0;
  fake_fixture->base.bytes_written_to_server_channel = 0;
  fake_fixture->base.bytes_read_from_client_channel = 0;
  fake_fixture->base.bytes_read_from_server_channel = 0;
  return &fake_fixture->base;
}

void fake_tsi_test_do_handshake_tiny_handshake_buffer() {
  tsi_test_fixture *fixture = fake_tsi_test_fixture_create();
  fixture->handshake_buffer_size = TSI_TEST_TINY_HANDSHAKE_BUFFER_SIZE;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void fake_tsi_test_do_handshake_small_handshake_buffer() {
  tsi_test_fixture *fixture = fake_tsi_test_fixture_create();
  fixture->handshake_buffer_size = TSI_TEST_SMALL_HANDSHAKE_BUFFER_SIZE;
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void fake_tsi_test_do_handshake() {
  tsi_test_fixture *fixture = fake_tsi_test_fixture_create();
  tsi_test_do_handshake(fixture);
  tsi_test_destroy(fixture);
}

void fake_tsi_test_do_round_trip_for_all_configs() {
  unsigned int *bit_array =
      gpr_zalloc(sizeof(unsigned int) * TSI_TEST_NUM_OF_ARGUMENTS);
  unsigned int mask = 1U << (TSI_TEST_NUM_OF_ARGUMENTS - 1);
  unsigned int val = 0, ind = 0;
  for (val = 0; val < TSI_TEST_NUM_OF_COMBINATIONS; val++) {
    unsigned int v = val;
    for (ind = 0; ind < TSI_TEST_NUM_OF_ARGUMENTS; ind++) {
      bit_array[ind] = (v & mask) ? 1 : 0;
      v <<= 1;
    }
    tsi_test_fixture *fixture = fake_tsi_test_fixture_create();
    fake_tsi_test_fixture *fake_fixture = (fake_tsi_test_fixture *)fixture;
    tsi_test_frame_protector_config_destroy(fake_fixture->base.config);
    fake_fixture->base.config = tsi_test_frame_protector_config_create(
        bit_array[0], bit_array[1], bit_array[2], bit_array[3], bit_array[4],
        bit_array[5], bit_array[6], bit_array[7]);
    tsi_test_do_round_trip(&fake_fixture->base);
    tsi_test_destroy(fixture);
  }
  gpr_free(bit_array);
}

void fake_tsi_test_do_round_trip_odd_buffer_size() {
  size_t odd_sizes[] = {1025, 2051, 4103, 8207, 16409};
  size_t size = sizeof(odd_sizes) / sizeof(size_t);
  size_t ind1 = 0, ind2 = 0, ind3 = 0, ind4 = 0, ind5 = 0;
  for (ind1 = 0; ind1 < size; ind1++) {
    for (ind2 = 0; ind2 < size; ind2++) {
      for (ind3 = 0; ind3 < size; ind3++) {
        for (ind4 = 0; ind4 < size; ind4++) {
          for (ind5 = 0; ind5 < size; ind5++) {
            tsi_test_fixture *fixture = fake_tsi_test_fixture_create();
            fake_tsi_test_fixture *fake_fixture =
                (fake_tsi_test_fixture *)fixture;
            tsi_test_frame_protector_config_set_buffer_size(
                fake_fixture->base.config, odd_sizes[ind1], odd_sizes[ind2],
                odd_sizes[ind3], odd_sizes[ind4], odd_sizes[ind5]);
            tsi_test_do_round_trip(&fake_fixture->base);
            tsi_test_destroy(fixture);
          }
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  fake_tsi_test_do_handshake_tiny_handshake_buffer();
  fake_tsi_test_do_handshake_small_handshake_buffer();
  fake_tsi_test_do_handshake();
  fake_tsi_test_do_round_trip_for_all_configs();
  fake_tsi_test_do_round_trip_odd_buffer_size();
  return 0;
}
