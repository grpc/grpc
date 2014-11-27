/*
 *
 * Copyright 2014, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/tsi/transport_security_test_lib.h"

#include <memory>

#include "base/commandlineflags.h"
#include "src/core/tsi/transport_security_interface.h"
#include "strings/escaping.h"
#include "strings/strcat.h"
#include <gtest/gtest.h>
#include "util/random/mt_random.h"

namespace {

const char kPingRequest[] = "Ping";
const char kPongResponse[] = "Pong";
const int kBigMessageSize = 17000;

}  // namespace

namespace tsi {
namespace test {

TransportSecurityTest::TransportSecurityTest() : random_(new MTRandom()) {
  small_message_ = "Chapi Chapo";
  big_message_ = RandomString(kBigMessageSize);
}

string TransportSecurityTest::RandomString(int size) {
  std::unique_ptr<char[]> buffer(new char[size]);
  for (int i = 0; i < size; i++) {
    buffer[i] = random_->Rand8();
  }
  return string(buffer.get(), size);
}

void TransportSecurityTest::SendBytesToPeer(bool is_client, unsigned char* buf,
                                            unsigned int buf_size) {
  string& channel = is_client ? to_server_channel_ : to_client_channel_;
  LOG(INFO) << (is_client ? "Client:" : "Server") << " sending " << buf_size
            << " bytes to peer.";
  channel.append(reinterpret_cast<const char*>(buf), buf_size);
}

void TransportSecurityTest::ReadBytesFromPeer(bool is_client,
                                              unsigned char* buf,
                                              unsigned int* buf_size) {
  string& channel = is_client ? to_client_channel_ : to_server_channel_;
  unsigned int to_read =
      *buf_size < channel.size() ? *buf_size : channel.size();
  memcpy(buf, channel.data(), to_read);
  *buf_size = to_read;
  channel.erase(0, to_read);
  LOG(INFO) << (is_client ? "Client:" : "Server") << " read " << to_read
            << " bytes from peer.";
}

void TransportSecurityTest::DoHandshakeStep(bool is_client,
                                            unsigned int buf_allocated_size,
                                            tsi_handshaker* handshaker,
                                            string* remaining_bytes) {
  tsi_result result = TSI_OK;
  std::unique_ptr<unsigned char[]> buf(new unsigned char[buf_allocated_size]);
  unsigned int buf_offset;
  unsigned int buf_size;
  // See if we need to send some bytes to the peer.
  do {
    unsigned int buf_size_to_send = buf_allocated_size;
    result = tsi_handshaker_get_bytes_to_send_to_peer(handshaker, buf.get(),
                                                      &buf_size_to_send);
    if (buf_size_to_send > 0) {
      SendBytesToPeer(is_client, buf.get(), buf_size_to_send);
    }
  } while (result == TSI_INCOMPLETE_DATA);
  if (!tsi_handshaker_is_in_progress(handshaker)) return;

  do {
    //  Read bytes from the peer.
    buf_size = buf_allocated_size;
    buf_offset = 0;
    ReadBytesFromPeer(is_client, buf.get(), &buf_size);
    if (buf_size == 0) break;

    // Process the bytes from the peer. We have to be careful as these bytes
    // may contain non-handshake data (protected data). If this is the case,
    // we will exit from the loop with buf_size > 0.
    unsigned int consumed_by_handshaker = buf_size;
    result = tsi_handshaker_process_bytes_from_peer(handshaker, buf.get(),
                                                    &consumed_by_handshaker);
    buf_size -= consumed_by_handshaker;
    buf_offset += consumed_by_handshaker;
  } while (result == TSI_INCOMPLETE_DATA);

  if (!tsi_handshaker_is_in_progress(handshaker)) {
    remaining_bytes->assign(
        reinterpret_cast<const char*>(buf.get()) + buf_offset, buf_size);
  }
}

void TransportSecurityTest::PerformHandshake() {
  SetupHandshakers();
  string remaining_bytes;
  do {
    DoHandshakeStep(true, config()->handshake_buffer_size,
                    client_handshaker_.get(), &remaining_bytes);
    EXPECT_EQ(0, remaining_bytes.size());
    DoHandshakeStep(false, config()->handshake_buffer_size,
                    server_handshaker_.get(), &remaining_bytes);
    EXPECT_EQ(0, remaining_bytes.size());
  } while (tsi_handshaker_is_in_progress(client_handshaker_.get()) ||
           tsi_handshaker_is_in_progress(server_handshaker_.get()));
  CheckHandshakeResults();
}

void TransportSecurityTest::SendMessageToPeer(
    bool is_client, tsi_frame_protector* protector, const string& message,
    unsigned int protected_buffer_size) {
  std::unique_ptr<unsigned char[]> protected_buffer(
      new unsigned char[protected_buffer_size]);
  unsigned int message_size = message.size();
  const unsigned char* message_bytes =
      reinterpret_cast<const unsigned char*>(message.data());
  tsi_result result = TSI_OK;
  while (message_size > 0 && result == TSI_OK) {
    unsigned int protected_buffer_size_to_send = protected_buffer_size;
    unsigned int processed_message_size = message_size;
    result = tsi_frame_protector_protect(
        protector, message_bytes, &processed_message_size,
        protected_buffer.get(), &protected_buffer_size_to_send);
    EXPECT_EQ(TSI_OK, result);
    SendBytesToPeer(is_client, protected_buffer.get(),
                    protected_buffer_size_to_send);
    message_bytes += processed_message_size;
    message_size -= processed_message_size;

    // Flush if we're done.
    if (message_size == 0) {
      unsigned int still_pending_size;
      do {
        protected_buffer_size_to_send = protected_buffer_size;
        result = tsi_frame_protector_protect_flush(
            protector, protected_buffer.get(), &protected_buffer_size_to_send,
            &still_pending_size);
        EXPECT_EQ(TSI_OK, result);
        SendBytesToPeer(is_client, protected_buffer.get(),
                        protected_buffer_size_to_send);
      } while (still_pending_size > 0 && result == TSI_OK);
      EXPECT_EQ(TSI_OK, result);
    }
  }
  EXPECT_EQ(TSI_OK, result);
}

void TransportSecurityTest::ReceiveMessageFromPeer(
    bool is_client, tsi_frame_protector* protector,
    unsigned int read_buf_allocated_size,
    unsigned int message_buf_allocated_size, string* message) {
  std::unique_ptr<unsigned char[]> read_buffer(
      new unsigned char[read_buf_allocated_size]);
  unsigned int read_offset = 0;
  unsigned int read_from_peer_size = 0;
  std::unique_ptr<unsigned char[]> message_buffer(
      new unsigned char[message_buf_allocated_size]);
  tsi_result result = TSI_OK;
  bool done = false;
  while (!done && result == TSI_OK) {
    if (read_from_peer_size == 0) {
      read_from_peer_size = read_buf_allocated_size;
      ReadBytesFromPeer(is_client, read_buffer.get(), &read_from_peer_size);
      read_offset = 0;
    }
    if (read_from_peer_size == 0) done = true;
    unsigned int message_buffer_size;
    do {
      message_buffer_size = message_buf_allocated_size;
      unsigned int processed_size = read_from_peer_size;
      result = tsi_frame_protector_unprotect(
          protector, read_buffer.get() + read_offset, &processed_size,
          message_buffer.get(), &message_buffer_size);
      EXPECT_EQ(TSI_OK, result);
      if (message_buffer_size > 0) {
        LOG(INFO) << "Wrote " << message_buffer_size << " bytes to message.";
        message->append(reinterpret_cast<const char*>(message_buffer.get()),
                        message_buffer_size);
      }
      read_offset += processed_size;
      read_from_peer_size -= processed_size;
    } while ((read_from_peer_size > 0 || message_buffer_size > 0) &&
             result == TSI_OK);
    EXPECT_EQ(TSI_OK, result);
  }
  EXPECT_EQ(TSI_OK, result);
}

void TransportSecurityTest::DoRoundTrip(const string& request,
                                        const string& response) {
  PerformHandshake();

  tsi_frame_protector* client_frame_protector;
  tsi_frame_protector* server_frame_protector;
  unsigned int client_max_output_protected_frame_size =
      config()->client_max_output_protected_frame_size();
  EXPECT_EQ(TSI_OK,
            tsi_handshaker_create_frame_protector(
                client_handshaker_.get(),
                config()->use_client_default_max_output_protected_frame_size()
                    ? nullptr
                    : &client_max_output_protected_frame_size,
                &client_frame_protector));

  unsigned int server_max_output_protected_frame_size =
      config()->server_max_output_protected_frame_size();
  EXPECT_EQ(TSI_OK,
            tsi_handshaker_create_frame_protector(
                server_handshaker_.get(),
                config()->use_server_default_max_output_protected_frame_size()
                    ? nullptr
                    : &server_max_output_protected_frame_size,
                &server_frame_protector));

  SendMessageToPeer(true, client_frame_protector, request,
                    config()->protected_buffer_size);
  string retrieved_request;
  ReceiveMessageFromPeer(
      false, server_frame_protector, config()->read_buffer_allocated_size,
      config()->message_buffer_allocated_size, &retrieved_request);
  EXPECT_EQ(request.size(), retrieved_request.size());
  EXPECT_EQ(strings::b2a_hex(request), strings::b2a_hex(retrieved_request));

  SendMessageToPeer(false, server_frame_protector, response,
                    config()->protected_buffer_size);
  string retrieved_response;
  ReceiveMessageFromPeer(
      true, client_frame_protector, config()->read_buffer_allocated_size,
      config()->message_buffer_allocated_size, &retrieved_response);
  EXPECT_EQ(response.size(), retrieved_response.size());
  EXPECT_EQ(strings::b2a_hex(response), strings::b2a_hex(retrieved_response));

  tsi_frame_protector_destroy(client_frame_protector);
  tsi_frame_protector_destroy(server_frame_protector);
}

void TransportSecurityTest::DoRoundTrip() {
  DoRoundTrip(config()->client_message, config()->server_message);
}
void TransportSecurityTest::PingPong() {
  PerformHandshake();

  unsigned char to_server[4096];
  unsigned char to_client[4096];
  unsigned int max_frame_size = sizeof(to_client);
  tsi_frame_protector* client_frame_protector;
  tsi_frame_protector* server_frame_protector;
  EXPECT_EQ(
      tsi_handshaker_create_frame_protector(
          client_handshaker_.get(), &max_frame_size, &client_frame_protector),
      TSI_OK);
  EXPECT_EQ(max_frame_size, sizeof(to_client));
  EXPECT_EQ(
      tsi_handshaker_create_frame_protector(
          server_handshaker_.get(), &max_frame_size, &server_frame_protector),
      TSI_OK);
  EXPECT_EQ(max_frame_size, sizeof(to_client));

  // Send Ping.
  unsigned int ping_length = strlen(kPingRequest);
  unsigned int protected_size = sizeof(to_server);
  EXPECT_EQ(tsi_frame_protector_protect(
                client_frame_protector,
                reinterpret_cast<const unsigned char*>(kPingRequest),
                &ping_length, to_server, &protected_size),
            TSI_OK);
  EXPECT_EQ(ping_length, strlen(kPingRequest));
  EXPECT_EQ(protected_size, 0);
  protected_size = sizeof(to_server);
  unsigned int still_pending_size;
  EXPECT_EQ(
      tsi_frame_protector_protect_flush(client_frame_protector, to_server,
                                        &protected_size, &still_pending_size),
      TSI_OK);
  EXPECT_EQ(still_pending_size, 0);
  EXPECT_GT(protected_size, strlen(kPingRequest));

  // Receive Ping.
  unsigned int unprotected_size = sizeof(to_server);
  unsigned int saved_protected_size = protected_size;
  EXPECT_EQ(tsi_frame_protector_unprotect(server_frame_protector, to_server,
                                          &protected_size, to_server,
                                          &unprotected_size),
            TSI_OK);
  EXPECT_EQ(saved_protected_size, protected_size);
  EXPECT_EQ(ping_length, unprotected_size);
  EXPECT_EQ(string(kPingRequest),
            string(reinterpret_cast<const char*>(to_server), unprotected_size));

  // Send back Pong.
  unsigned int pong_length = strlen(kPongResponse);
  protected_size = sizeof(to_client);
  EXPECT_EQ(tsi_frame_protector_protect(
                server_frame_protector,
                reinterpret_cast<const unsigned char*>(kPongResponse),
                &pong_length, to_client, &protected_size),
            TSI_OK);
  EXPECT_EQ(pong_length, strlen(kPongResponse));
  EXPECT_EQ(protected_size, 0);
  protected_size = sizeof(to_client);
  EXPECT_EQ(
      tsi_frame_protector_protect_flush(server_frame_protector, to_client,
                                        &protected_size, &still_pending_size),
      TSI_OK);
  EXPECT_EQ(still_pending_size, 0);
  EXPECT_GT(protected_size, strlen(kPongResponse));

  // Receive Pong.
  unprotected_size = sizeof(to_server);
  saved_protected_size = protected_size;
  EXPECT_EQ(tsi_frame_protector_unprotect(client_frame_protector, to_client,
                                          &protected_size, to_client,
                                          &unprotected_size),
            TSI_OK);
  EXPECT_EQ(saved_protected_size, protected_size);
  EXPECT_EQ(pong_length, unprotected_size);
  EXPECT_EQ(string(kPongResponse),
            string(reinterpret_cast<const char*>(to_client), unprotected_size));

  tsi_frame_protector_destroy(client_frame_protector);
  tsi_frame_protector_destroy(server_frame_protector);
}

}  // namespace test
}  // namespace tsi
