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
