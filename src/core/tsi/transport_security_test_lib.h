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

#ifndef __TRANSPORT_SECURITY_TEST_LIB_H_
#define __TRANSPORT_SECURITY_TEST_LIB_H_

#include <memory>

#include "base/commandlineflags.h"
#include "src/core/tsi/transport_security_interface.h"
#include "strings/strcat.h"
#include <gtest/gtest.h>
#include "util/random/mt_random.h"

namespace tsi {
namespace test {

class TestConfig {
 public:
  TestConfig()
      : client_message("Chapi Chapo"),
        server_message("Chapi Chapo"),
        handshake_buffer_size(4096),
        read_buffer_allocated_size(4096),
        message_buffer_allocated_size(4096),
        protected_buffer_size(16384),
        use_client_default_max_output_protected_frame_size_(true),
        use_server_default_max_output_protected_frame_size_(true),
        client_max_output_protected_frame_size_(0),
        server_max_output_protected_frame_size_(0) {}

  void set_client_max_output_protected_frame_size(unsigned int size) {
    use_client_default_max_output_protected_frame_size_ = false;
    client_max_output_protected_frame_size_ = size;
  }
  void set_server_max_output_protected_frame_size(unsigned int size) {
    use_server_default_max_output_protected_frame_size_ = false;
    server_max_output_protected_frame_size_ = size;
  }
  bool use_client_default_max_output_protected_frame_size() const {
    return use_client_default_max_output_protected_frame_size_;
  }
  bool use_server_default_max_output_protected_frame_size() const {
    return use_server_default_max_output_protected_frame_size_;
  }
  unsigned int client_max_output_protected_frame_size() const {
    return client_max_output_protected_frame_size_;
  }
  unsigned int server_max_output_protected_frame_size() const {
    return server_max_output_protected_frame_size_;
  }

  string client_message;
  string server_message;
  unsigned int handshake_buffer_size;
  unsigned int read_buffer_allocated_size;
  unsigned int message_buffer_allocated_size;
  unsigned int protected_buffer_size;

 private:
  bool use_client_default_max_output_protected_frame_size_;
  bool use_server_default_max_output_protected_frame_size_;
  unsigned int client_max_output_protected_frame_size_;
  unsigned int server_max_output_protected_frame_size_;
};


struct TsiHandshakerDeleter {
  inline void operator()(tsi_handshaker* ptr) { tsi_handshaker_destroy(ptr); }
};
typedef std::unique_ptr<tsi_handshaker, TsiHandshakerDeleter>
    TsiHandshakerUniquePtr;

class TransportSecurityTest : public ::testing::Test {
 protected:
  TransportSecurityTest();
  virtual ~TransportSecurityTest() {}
  virtual const TestConfig* config() = 0;
  string RandomString(int size);
  virtual void SetupHandshakers() = 0;
  // An implementation-specific verification of the validity of the handshake.
  virtual void CheckHandshakeResults() = 0;
  // Do a full handshake.
  void PerformHandshake();
  // Send a protected message between the client and server.
  void SendMessageToPeer(bool is_client, tsi_frame_protector* protector,
                         const string& message,
                         unsigned int protected_buffer_size);
  void ReceiveMessageFromPeer(bool is_client, tsi_frame_protector* protector,
                              unsigned int read_buf_allocated_size,
                              unsigned int message_buf_allocated_size,
                              string* message);

  // A simple test that does a handshake and sends a message back and forth
  void PingPong();
  // A complicated test that can be configured by modifying config().
  void DoRoundTrip();

  TsiHandshakerUniquePtr client_handshaker_;
  TsiHandshakerUniquePtr server_handshaker_;

  string small_message_;
  string big_message_;
  std::unique_ptr<RandomBase> random_;

 private:
  // Functions to send raw bytes between the client and server.
  void SendBytesToPeer(bool is_client, unsigned char* buf,
                       unsigned int buf_size);
  void ReadBytesFromPeer(bool is_client, unsigned char* buf,
                         unsigned int* buf_size);
  // Do a single step of the handshake.
  void DoHandshakeStep(bool is_client, unsigned int buf_allocated_size,
                       tsi_handshaker* handshaker, string* remaining_bytes);
  void DoRoundTrip(const string& request, const string& response);

  string to_server_channel_;
  string to_client_channel_;
};

}  // namespace test
}  // namespace tsi

#endif  // __TRANSPORT_SECURITY_TEST_LIB_H_
