/*
 *
 * Copyright 2017, Google Inc.
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

#include <grpc++/impl/codegen/proto_utils.h>
#include <grpc++/impl/grpc_library.h>
#include <gtest/gtest.h>

namespace grpc {
namespace internal {

static GrpcLibraryInitializer g_gli_initializer;

// Provide access to GrpcBufferWriter internals.
class GrpcBufferWriterPeer {
 public:
  explicit GrpcBufferWriterPeer(internal::GrpcBufferWriter* writer)
      : writer_(writer) {}
  bool have_backup() const { return writer_->have_backup_; }
  const grpc_slice& backup_slice() const { return writer_->backup_slice_; }
  const grpc_slice& slice() const { return writer_->slice_; }

 private:
  GrpcBufferWriter* writer_;
};

class ProtoUtilsTest : public ::testing::Test {};

// Regression test for a memory corruption bug where a series of
// GrpcBufferWriter Next()/Backup() invocations could result in a dangling
// pointer returned by Next() due to the interaction between grpc_slice inlining
// and GRPC_SLICE_START_PTR.
TEST_F(ProtoUtilsTest, BackupNext) {
  // Ensure the GrpcBufferWriter internals are initialized.
  g_gli_initializer.summon();

  grpc_byte_buffer* bp;
  GrpcBufferWriter writer(&bp, 8192);
  GrpcBufferWriterPeer peer(&writer);

  void* data;
  int size;
  // Allocate a slice.
  ASSERT_TRUE(writer.Next(&data, &size));
  EXPECT_EQ(8192, size);
  // Return a single byte. Before the fix that this test acts as a regression
  // for, this would have resulted in an inlined backup slice.
  writer.BackUp(1);
  EXPECT_TRUE(!peer.have_backup());
  // On the next allocation, the slice is non-inlined.
  ASSERT_TRUE(writer.Next(&data, &size));
  EXPECT_TRUE(peer.slice().refcount != NULL);

  // Cleanup.
  g_core_codegen_interface->grpc_byte_buffer_destroy(bp);
}

}  // namespace internal
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
