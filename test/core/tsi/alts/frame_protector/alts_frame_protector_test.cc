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

#include "src/core/tsi/alts/frame_protector/alts_frame_protector.h"

#include <stdbool.h>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/tsi/alts/crypt/gsec.h"
#include "src/core/tsi/transport_security_interface.h"
#include "test/core/tsi/alts/crypt/gsec_test_util.h"
#include "test/core/tsi/transport_security_test_lib.h"

const size_t kChannelSize = 32768;

static void alts_test_do_round_trip_check_frames(
    tsi_test_frame_protector_fixture* fixture, const uint8_t* key,
    const size_t key_size, bool rekey, const uint8_t* client_message,
    const size_t client_message_size, const uint8_t* client_expected_frames,
    const size_t client_frame_size, const uint8_t* server_message,
    const size_t server_message_size, const uint8_t* server_expected_frames,
    const size_t server_frame_size) {
  ASSERT_NE(fixture, nullptr);
  ASSERT_NE(fixture->config, nullptr);
  tsi_frame_protector* client_frame_protector = nullptr;
  tsi_frame_protector* server_frame_protector = nullptr;
  tsi_test_frame_protector_config* config = fixture->config;
  tsi_test_channel* channel = fixture->channel;
  // Create a client frame protector.
  size_t client_max_output_protected_frame_size =
      config->client_max_output_protected_frame_size;
  ASSERT_EQ(
      alts_create_frame_protector(key, key_size, /*is_client=*/true, rekey,
                                  client_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &client_max_output_protected_frame_size,
                                  &client_frame_protector),
      TSI_OK);  // Create a server frame protector.
  size_t server_max_output_protected_frame_size =
      config->server_max_output_protected_frame_size;
  ASSERT_EQ(
      alts_create_frame_protector(key, key_size, /*is_client=*/false, rekey,
                                  server_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &server_max_output_protected_frame_size,
                                  &server_frame_protector),
      TSI_OK);
  tsi_test_frame_protector_fixture_init(fixture, client_frame_protector,
                                        server_frame_protector);
  // Client sends a message to server.
  uint8_t* saved_client_message = config->client_message;
  config->client_message = const_cast<uint8_t*>(client_message);
  config->client_message_size = client_message_size;
  tsi_test_frame_protector_send_message_to_peer(config, channel,
                                                client_frame_protector,
                                                /*is_client=*/true);
  // Verify if the generated frame is the same as the expected.
  ASSERT_EQ(channel->bytes_written_to_server_channel, client_frame_size);
  ASSERT_EQ(memcmp(client_expected_frames, channel->server_channel,
                   client_frame_size),
            0);
  unsigned char* server_received_message =
      static_cast<unsigned char*>(gpr_malloc(kChannelSize));
  size_t server_received_message_size = 0;
  tsi_test_frame_protector_receive_message_from_peer(
      config, channel, server_frame_protector, server_received_message,
      &server_received_message_size, /*is_client=*/false);
  ASSERT_EQ(config->client_message_size, server_received_message_size);
  ASSERT_EQ(memcmp(config->client_message, server_received_message,
                   server_received_message_size),
            0);
  // Server sends a message to client.
  uint8_t* saved_server_message = config->server_message;
  config->server_message = const_cast<uint8_t*>(server_message);
  config->server_message_size = server_message_size;
  tsi_test_frame_protector_send_message_to_peer(config, channel,
                                                server_frame_protector,
                                                /*is_client=*/false);
  // Verify if the generated frame is the same as the expected.
  ASSERT_EQ(channel->bytes_written_to_client_channel, server_frame_size);
  ASSERT_EQ(memcmp(server_expected_frames, channel->client_channel,
                   server_frame_size),
            0);
  unsigned char* client_received_message =
      static_cast<unsigned char*>(gpr_malloc(kChannelSize));
  size_t client_received_message_size = 0;
  tsi_test_frame_protector_receive_message_from_peer(
      config, channel, client_frame_protector, client_received_message,
      &client_received_message_size,
      /*is_client=*/true);
  ASSERT_EQ(config->server_message_size, client_received_message_size);
  ASSERT_EQ(memcmp(config->server_message, client_received_message,
                   client_received_message_size),
            0);
  config->client_message = saved_client_message;
  config->server_message = saved_server_message;
  // Destroy server and client frame protectors.
  gpr_free(server_received_message);
  gpr_free(client_received_message);
}

static void alts_test_do_round_trip_vector_tests() {
  const uint8_t key[] = {0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
                         0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08};
  const char small_message[] = {'C', 'h', 'a', 'p', 'i', ' ',
                                'C', 'h', 'a', 'p', 'o'};
  const uint8_t large_message[] = {
      0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5, 0xa5, 0x59, 0x09, 0xc5,
      0xaf, 0xf5, 0x26, 0x9a, 0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
      0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72, 0x1c, 0x3c, 0x0c, 0x95,
      0x95, 0x68, 0x09, 0x53, 0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
      0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57, 0xba, 0x63, 0x7b, 0x39,
      0x1a, 0xaf, 0xd2, 0x55, 0xd6, 0x09, 0xb1, 0xf0, 0x56, 0x63, 0x7a, 0x0d,
      0x46, 0xdf, 0x99, 0x8d, 0x88, 0xe5, 0x22, 0x2a, 0xb2, 0xc2, 0x84, 0x65,
      0x12, 0x15, 0x35, 0x24, 0xc0, 0x89, 0x5e, 0x81, 0x08, 0x06, 0x0f, 0x10,
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
      0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
      0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30};
  const size_t small_message_size = sizeof(small_message) / sizeof(uint8_t);
  const size_t large_message_size = sizeof(large_message) / sizeof(uint8_t);
  // Test small client message and large server message.
  const uint8_t client_expected_frame1[] = {
      0x1f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x09, 0xd8, 0xd5, 0x92,
      0x4d, 0x50, 0x32, 0xb7, 0x1f, 0xb8, 0xf2, 0xbb, 0x43, 0xc7, 0xe2, 0x94,
      0x3d, 0x3e, 0x9a, 0x78, 0x76, 0xaa, 0x0a, 0x6b, 0xfa, 0x98, 0x3a};
  const uint8_t server_expected_frame1[] = {
      0x94, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xa9, 0x4b, 0xf8, 0xc8,
      0xe7, 0x8f, 0x1a, 0x26, 0x37, 0x44, 0xa2, 0x5c, 0x55, 0x94, 0x30, 0x4e,
      0x3e, 0x16, 0xe7, 0x9e, 0x96, 0xe8, 0x1b, 0xc0, 0xdd, 0x52, 0x30, 0x06,
      0xc2, 0x72, 0x9a, 0xa1, 0x0b, 0xdb, 0xdc, 0x19, 0x8c, 0x93, 0x5e, 0x84,
      0x1f, 0x4b, 0x97, 0x26, 0xf0, 0x73, 0x85, 0x59, 0x00, 0x95, 0xc1, 0xc5,
      0x22, 0x2f, 0x70, 0x85, 0x68, 0x2c, 0x4f, 0xfe, 0x30, 0x26, 0x91, 0xde,
      0x62, 0x55, 0x1d, 0x35, 0x01, 0x96, 0x1c, 0xe7, 0xa2, 0x8b, 0x14, 0x8a,
      0x5e, 0x1b, 0x4a, 0x3b, 0x4f, 0x65, 0x0f, 0xca, 0x79, 0x10, 0xb4, 0xdd,
      0xf7, 0xa4, 0x8b, 0x64, 0x2f, 0x00, 0x39, 0x60, 0x03, 0xfc, 0xe1, 0x8b,
      0x5c, 0x19, 0xba, 0xcc, 0x46, 0xba, 0x88, 0xdd, 0x40, 0x42, 0x27, 0x4f,
      0xe4, 0x1a, 0x6a, 0x31, 0x6c, 0x1c, 0xb0, 0xb6, 0x5c, 0x3e, 0xca, 0x84,
      0x9b, 0x5f, 0x04, 0x84, 0x11, 0xa9, 0xf8, 0x39, 0xe7, 0xe7, 0xc5, 0xc4,
      0x33, 0x9f, 0x63, 0x21, 0x9a, 0x7c, 0x9c, 0x64};
  const size_t client_frame_size1 =
      sizeof(client_expected_frame1) / sizeof(uint8_t);
  const size_t server_frame_size1 =
      sizeof(server_expected_frame1) / sizeof(uint8_t);
  tsi_test_frame_protector_fixture* fixture =
      tsi_test_frame_protector_fixture_create();
  alts_test_do_round_trip_check_frames(
      fixture, key, kAes128GcmKeyLength, /*rekey=*/false,
      reinterpret_cast<const uint8_t*>(small_message), small_message_size,
      client_expected_frame1, client_frame_size1, large_message,
      large_message_size, server_expected_frame1, server_frame_size1);
  tsi_test_frame_protector_fixture_destroy(fixture);
  ///
  /// Test large client message, small server message, and small
  /// message_buffer_allocated_size.
  ///
  const uint8_t client_expected_frame2[] = {
      0x94, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x93, 0x81, 0x86, 0xc7,
      0xdc, 0xf4, 0x77, 0x3a, 0xdb, 0x91, 0x94, 0x61, 0xba, 0xed, 0xd5, 0x37,
      0x47, 0x53, 0x0c, 0xe1, 0xbf, 0x59, 0x23, 0x20, 0xde, 0x8b, 0x25, 0x13,
      0x72, 0xe7, 0x8a, 0x4f, 0x32, 0x61, 0xc6, 0xda, 0xc3, 0xe9, 0xff, 0x31,
      0x33, 0x53, 0x4a, 0xf8, 0xc9, 0x98, 0xe4, 0x19, 0x71, 0x9c, 0x5e, 0x72,
      0xc7, 0x35, 0x97, 0x78, 0x30, 0xf2, 0xc4, 0xd1, 0x53, 0xd5, 0x6e, 0x8f,
      0x4f, 0xd9, 0x28, 0x5a, 0xfd, 0x22, 0x57, 0x7f, 0x95, 0xb4, 0x8a, 0x5e,
      0x7c, 0x47, 0xa8, 0xcf, 0x64, 0x3d, 0x83, 0xa5, 0xcf, 0xc3, 0xfe, 0x54,
      0xc2, 0x6a, 0x40, 0xc4, 0xfb, 0x8e, 0x07, 0x77, 0x70, 0x8f, 0x99, 0x94,
      0xb1, 0xd5, 0xa7, 0xf9, 0x0d, 0xc7, 0x11, 0xc5, 0x6f, 0x4a, 0x4f, 0x56,
      0xd5, 0xe2, 0x9c, 0xbb, 0x95, 0x7a, 0xd0, 0x9f, 0x30, 0x54, 0xca, 0x6d,
      0x5c, 0x8e, 0x83, 0xa0, 0x04, 0x5e, 0xd0, 0x22, 0x8c, 0x2a, 0x7f, 0xdb,
      0xfe, 0xb3, 0x2e, 0xae, 0x22, 0xe6, 0xf4, 0xb7};
  const uint8_t server_expected_frame2[] = {
      0x1f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x33, 0x12, 0xab, 0x9d,
      0x76, 0x2b, 0x5f, 0xab, 0xf3, 0x6d, 0xc4, 0xaa, 0xe5, 0x1e, 0x63, 0xc1,
      0x7b, 0x7b, 0x10, 0xd5, 0x63, 0x0f, 0x29, 0xad, 0x17, 0x33, 0x73};
  const size_t client_frame_size2 =
      sizeof(client_expected_frame2) / sizeof(uint8_t);
  const size_t server_frame_size2 =
      sizeof(server_expected_frame2) / sizeof(uint8_t);
  fixture = tsi_test_frame_protector_fixture_create();
  alts_test_do_round_trip_check_frames(
      fixture, key, kAes128GcmKeyLength, /*rekey=*/false, large_message,
      large_message_size, client_expected_frame2, client_frame_size2,
      reinterpret_cast<const uint8_t*>(small_message), small_message_size,
      server_expected_frame2, server_frame_size2);
  tsi_test_frame_protector_fixture_destroy(fixture);
  ///
  /// Test large client message, small server message, and small
  /// protected_buffer_size.
  ///
  const uint8_t client_expected_frame3[] = {
      0x94, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x93, 0x81, 0x86, 0xc7,
      0xdc, 0xf4, 0x77, 0x3a, 0xdb, 0x91, 0x94, 0x61, 0xba, 0xed, 0xd5, 0x37,
      0x47, 0x53, 0x0c, 0xe1, 0xbf, 0x59, 0x23, 0x20, 0xde, 0x8b, 0x25, 0x13,
      0x72, 0xe7, 0x8a, 0x4f, 0x32, 0x61, 0xc6, 0xda, 0xc3, 0xe9, 0xff, 0x31,
      0x33, 0x53, 0x4a, 0xf8, 0xc9, 0x98, 0xe4, 0x19, 0x71, 0x9c, 0x5e, 0x72,
      0xc7, 0x35, 0x97, 0x78, 0x30, 0xf2, 0xc4, 0xd1, 0x53, 0xd5, 0x6e, 0x8f,
      0x4f, 0xd9, 0x28, 0x5a, 0xfd, 0x22, 0x57, 0x7f, 0x95, 0xb4, 0x8a, 0x5e,
      0x7c, 0x47, 0xa8, 0xcf, 0x64, 0x3d, 0x83, 0xa5, 0xcf, 0xc3, 0xfe, 0x54,
      0xc2, 0x6a, 0x40, 0xc4, 0xfb, 0x8e, 0x07, 0x77, 0x70, 0x8f, 0x99, 0x94,
      0xb1, 0xd5, 0xa7, 0xf9, 0x0d, 0xc7, 0x11, 0xc5, 0x6f, 0x4a, 0x4f, 0x56,
      0xd5, 0xe2, 0x9c, 0xbb, 0x95, 0x7a, 0xd0, 0x9f, 0x30, 0x54, 0xca, 0x6d,
      0x5c, 0x8e, 0x83, 0xa0, 0x04, 0x5e, 0xd0, 0x22, 0x8c, 0x2a, 0x7f, 0xdb,
      0xfe, 0xb3, 0x2e, 0xae, 0x22, 0xe6, 0xf4, 0xb7};
  const uint8_t server_expected_frame3[] = {
      0x1f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x33, 0x12, 0xab, 0x9d,
      0x76, 0x2b, 0x5f, 0xab, 0xf3, 0x6d, 0xc4, 0xaa, 0xe5, 0x1e, 0x63, 0xc1,
      0x7b, 0x7b, 0x10, 0xd5, 0x63, 0x0f, 0x29, 0xad, 0x17, 0x33, 0x73};
  const size_t client_frame_size3 =
      sizeof(client_expected_frame3) / sizeof(uint8_t);
  const size_t server_frame_size3 =
      sizeof(server_expected_frame3) / sizeof(uint8_t);
  fixture = tsi_test_frame_protector_fixture_create();
  alts_test_do_round_trip_check_frames(
      fixture, key, kAes128GcmKeyLength, /*rekey=*/false, large_message,
      large_message_size, client_expected_frame3, client_frame_size3,
      reinterpret_cast<const uint8_t*>(small_message), small_message_size,
      server_expected_frame3, server_frame_size3);
  tsi_test_frame_protector_fixture_destroy(fixture);
  ///
  /// Test large client message, small server message, and small
  /// read_buffer_allocated_size.
  ///
  const uint8_t client_expected_frame4[] = {
      0x94, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x93, 0x81, 0x86, 0xc7,
      0xdc, 0xf4, 0x77, 0x3a, 0xdb, 0x91, 0x94, 0x61, 0xba, 0xed, 0xd5, 0x37,
      0x47, 0x53, 0x0c, 0xe1, 0xbf, 0x59, 0x23, 0x20, 0xde, 0x8b, 0x25, 0x13,
      0x72, 0xe7, 0x8a, 0x4f, 0x32, 0x61, 0xc6, 0xda, 0xc3, 0xe9, 0xff, 0x31,
      0x33, 0x53, 0x4a, 0xf8, 0xc9, 0x98, 0xe4, 0x19, 0x71, 0x9c, 0x5e, 0x72,
      0xc7, 0x35, 0x97, 0x78, 0x30, 0xf2, 0xc4, 0xd1, 0x53, 0xd5, 0x6e, 0x8f,
      0x4f, 0xd9, 0x28, 0x5a, 0xfd, 0x22, 0x57, 0x7f, 0x95, 0xb4, 0x8a, 0x5e,
      0x7c, 0x47, 0xa8, 0xcf, 0x64, 0x3d, 0x83, 0xa5, 0xcf, 0xc3, 0xfe, 0x54,
      0xc2, 0x6a, 0x40, 0xc4, 0xfb, 0x8e, 0x07, 0x77, 0x70, 0x8f, 0x99, 0x94,
      0xb1, 0xd5, 0xa7, 0xf9, 0x0d, 0xc7, 0x11, 0xc5, 0x6f, 0x4a, 0x4f, 0x56,
      0xd5, 0xe2, 0x9c, 0xbb, 0x95, 0x7a, 0xd0, 0x9f, 0x30, 0x54, 0xca, 0x6d,
      0x5c, 0x8e, 0x83, 0xa0, 0x04, 0x5e, 0xd0, 0x22, 0x8c, 0x2a, 0x7f, 0xdb,
      0xfe, 0xb3, 0x2e, 0xae, 0x22, 0xe6, 0xf4, 0xb7};
  const uint8_t server_expected_frame4[] = {
      0x1f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x33, 0x12, 0xab, 0x9d,
      0x76, 0x2b, 0x5f, 0xab, 0xf3, 0x6d, 0xc4, 0xaa, 0xe5, 0x1e, 0x63, 0xc1,
      0x7b, 0x7b, 0x10, 0xd5, 0x63, 0x0f, 0x29, 0xad, 0x17, 0x33, 0x73};
  const size_t client_frame_size4 =
      sizeof(client_expected_frame4) / sizeof(uint8_t);
  const size_t server_frame_size4 =
      sizeof(server_expected_frame4) / sizeof(uint8_t);
  fixture = tsi_test_frame_protector_fixture_create();
  alts_test_do_round_trip_check_frames(
      fixture, key, kAes128GcmKeyLength, /*rekey=*/false, large_message,
      large_message_size, client_expected_frame4, client_frame_size4,
      reinterpret_cast<const uint8_t*>(small_message), small_message_size,
      server_expected_frame4, server_frame_size4);
  tsi_test_frame_protector_fixture_destroy(fixture);
  ///
  /// Test large client message, small server message, and small
  /// client_max_output_protected_frame_size.
  ///
  const uint8_t client_expected_frame5[] = {
      0x94, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x93, 0x81, 0x86, 0xc7,
      0xdc, 0xf4, 0x77, 0x3a, 0xdb, 0x91, 0x94, 0x61, 0xba, 0xed, 0xd5, 0x37,
      0x47, 0x53, 0x0c, 0xe1, 0xbf, 0x59, 0x23, 0x20, 0xde, 0x8b, 0x25, 0x13,
      0x72, 0xe7, 0x8a, 0x4f, 0x32, 0x61, 0xc6, 0xda, 0xc3, 0xe9, 0xff, 0x31,
      0x33, 0x53, 0x4a, 0xf8, 0xc9, 0x98, 0xe4, 0x19, 0x71, 0x9c, 0x5e, 0x72,
      0xc7, 0x35, 0x97, 0x78, 0x30, 0xf2, 0xc4, 0xd1, 0x53, 0xd5, 0x6e, 0x8f,
      0x4f, 0xd9, 0x28, 0x5a, 0xfd, 0x22, 0x57, 0x7f, 0x95, 0xb4, 0x8a, 0x5e,
      0x7c, 0x47, 0xa8, 0xcf, 0x64, 0x3d, 0x83, 0xa5, 0xcf, 0xc3, 0xfe, 0x54,
      0xc2, 0x6a, 0x40, 0xc4, 0xfb, 0x8e, 0x07, 0x77, 0x70, 0x8f, 0x99, 0x94,
      0xb1, 0xd5, 0xa7, 0xf9, 0x0d, 0xc7, 0x11, 0xc5, 0x6f, 0x4a, 0x4f, 0x56,
      0xd5, 0xe2, 0x9c, 0xbb, 0x95, 0x7a, 0xd0, 0x9f, 0x30, 0x54, 0xca, 0x6d,
      0x5c, 0x8e, 0x83, 0xa0, 0x04, 0x5e, 0xd0, 0x22, 0x8c, 0x2a, 0x7f, 0xdb,
      0xfe, 0xb3, 0x2e, 0xae, 0x22, 0xe6, 0xf4, 0xb7};
  const uint8_t server_expected_frame5[] = {
      0x1f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x33, 0x12, 0xab, 0x9d,
      0x76, 0x2b, 0x5f, 0xab, 0xf3, 0x6d, 0xc4, 0xaa, 0xe5, 0x1e, 0x63, 0xc1,
      0x7b, 0x7b, 0x10, 0xd5, 0x63, 0x0f, 0x29, 0xad, 0x17, 0x33, 0x73};
  const size_t client_frame_size5 =
      sizeof(client_expected_frame5) / sizeof(uint8_t);
  const size_t server_frame_size5 =
      sizeof(server_expected_frame5) / sizeof(uint8_t);
  fixture = tsi_test_frame_protector_fixture_create();
  alts_test_do_round_trip_check_frames(
      fixture, key, kAes128GcmKeyLength, /*rekey=*/false, large_message,
      large_message_size, client_expected_frame5, client_frame_size5,
      reinterpret_cast<const uint8_t*>(small_message), small_message_size,
      server_expected_frame5, server_frame_size5);
  tsi_test_frame_protector_fixture_destroy(fixture);
  ///
  /// Test small client message, large server message, and small
  /// server_max_output_protected_frame_size.
  ///
  const uint8_t client_expected_frame6[] = {
      0x1f, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x09, 0xd8, 0xd5, 0x92,
      0x4d, 0x50, 0x32, 0xb7, 0x1f, 0xb8, 0xf2, 0xbb, 0x43, 0xc7, 0xe2, 0x94,
      0x3d, 0x3e, 0x9a, 0x78, 0x76, 0xaa, 0x0a, 0x6b, 0xfa, 0x98, 0x3a};
  const uint8_t server_expected_frame6[] = {
      0x94, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xa9, 0x4b, 0xf8, 0xc8,
      0xe7, 0x8f, 0x1a, 0x26, 0x37, 0x44, 0xa2, 0x5c, 0x55, 0x94, 0x30, 0x4e,
      0x3e, 0x16, 0xe7, 0x9e, 0x96, 0xe8, 0x1b, 0xc0, 0xdd, 0x52, 0x30, 0x06,
      0xc2, 0x72, 0x9a, 0xa1, 0x0b, 0xdb, 0xdc, 0x19, 0x8c, 0x93, 0x5e, 0x84,
      0x1f, 0x4b, 0x97, 0x26, 0xf0, 0x73, 0x85, 0x59, 0x00, 0x95, 0xc1, 0xc5,
      0x22, 0x2f, 0x70, 0x85, 0x68, 0x2c, 0x4f, 0xfe, 0x30, 0x26, 0x91, 0xde,
      0x62, 0x55, 0x1d, 0x35, 0x01, 0x96, 0x1c, 0xe7, 0xa2, 0x8b, 0x14, 0x8a,
      0x5e, 0x1b, 0x4a, 0x3b, 0x4f, 0x65, 0x0f, 0xca, 0x79, 0x10, 0xb4, 0xdd,
      0xf7, 0xa4, 0x8b, 0x64, 0x2f, 0x00, 0x39, 0x60, 0x03, 0xfc, 0xe1, 0x8b,
      0x5c, 0x19, 0xba, 0xcc, 0x46, 0xba, 0x88, 0xdd, 0x40, 0x42, 0x27, 0x4f,
      0xe4, 0x1a, 0x6a, 0x31, 0x6c, 0x1c, 0xb0, 0xb6, 0x5c, 0x3e, 0xca, 0x84,
      0x9b, 0x5f, 0x04, 0x84, 0x11, 0xa9, 0xf8, 0x39, 0xe7, 0xe7, 0xc5, 0xc4,
      0x33, 0x9f, 0x63, 0x21, 0x9a, 0x7c, 0x9c, 0x64};
  const size_t client_frame_size6 =
      sizeof(client_expected_frame6) / sizeof(uint8_t);
  const size_t server_frame_size6 =
      sizeof(server_expected_frame6) / sizeof(uint8_t);
  fixture = tsi_test_frame_protector_fixture_create();
  alts_test_do_round_trip_check_frames(
      fixture, key, kAes128GcmKeyLength, /*rekey=*/false,
      reinterpret_cast<const uint8_t*>(small_message), small_message_size,
      client_expected_frame6, client_frame_size6, large_message,
      large_message_size, server_expected_frame6, server_frame_size6);
  tsi_test_frame_protector_fixture_destroy(fixture);
}

static void alts_test_do_round_trip(tsi_test_frame_protector_fixture* fixture,
                                    bool rekey) {
  ASSERT_NE(fixture, nullptr);
  ASSERT_NE(fixture->config, nullptr);
  tsi_frame_protector* client_frame_protector = nullptr;
  tsi_frame_protector* server_frame_protector = nullptr;
  tsi_test_frame_protector_config* config = fixture->config;
  // Create a key to be used by both client and server.
  uint8_t* key = nullptr;
  size_t key_length = rekey ? kAes128GcmRekeyKeyLength : kAes128GcmKeyLength;
  gsec_test_random_array(&key, key_length);
  // Create a client frame protector.
  size_t client_max_output_protected_frame_size =
      config->client_max_output_protected_frame_size;
  ASSERT_EQ(
      alts_create_frame_protector(key, key_length, /*is_client=*/true, rekey,
                                  client_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &client_max_output_protected_frame_size,
                                  &client_frame_protector),
      TSI_OK);
  // Create a server frame protector.
  size_t server_max_output_protected_frame_size =
      config->server_max_output_protected_frame_size;
  ASSERT_EQ(
      alts_create_frame_protector(key, key_length, /*is_client=*/false, rekey,
                                  server_max_output_protected_frame_size == 0
                                      ? nullptr
                                      : &server_max_output_protected_frame_size,
                                  &server_frame_protector),
      TSI_OK);
  tsi_test_frame_protector_fixture_init(fixture, client_frame_protector,
                                        server_frame_protector);
  tsi_test_frame_protector_do_round_trip_no_handshake(fixture);
  gpr_free(key);
}

// Run all combinations of different arguments of test config.
static void alts_test_do_round_trip_all(bool rekey) {
  unsigned int* bit_array = static_cast<unsigned int*>(
      gpr_malloc(sizeof(unsigned int) * TSI_TEST_NUM_OF_ARGUMENTS));
  unsigned int mask = 1U << (TSI_TEST_NUM_OF_ARGUMENTS - 1);
  unsigned int val = 0, ind = 0;
  for (val = 0; val < TSI_TEST_NUM_OF_COMBINATIONS; val++) {
    unsigned int v = val;
    for (ind = 0; ind < TSI_TEST_NUM_OF_ARGUMENTS; ind++) {
      bit_array[ind] = (v & mask) ? 1 : 0;
      v <<= 1;
    }
    tsi_test_frame_protector_fixture* fixture =
        tsi_test_frame_protector_fixture_create();
    tsi_test_frame_protector_config_destroy(fixture->config);
    fixture->config = tsi_test_frame_protector_config_create(
        bit_array[0], bit_array[1], bit_array[2], bit_array[3], bit_array[4],
        bit_array[5], bit_array[6]);
    alts_test_do_round_trip(fixture, rekey);
    tsi_test_frame_protector_fixture_destroy(fixture);
  }
  gpr_free(bit_array);
}

TEST(AltsFrameProtectorTest, MainTest) {
  alts_test_do_round_trip_vector_tests();
  alts_test_do_round_trip_all(/*rekey=*/false);
  alts_test_do_round_trip_all(/*rekey=*/true);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
