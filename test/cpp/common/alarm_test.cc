/*
 *
 * Copyright 2015, Google Inc.
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

#include <grpc++/alarm.h>
#include <grpc++/completion_queue.h>
#include <gtest/gtest.h>

#include "test/core/util/test_config.h"

namespace grpc {
namespace {

class TestTag : public CompletionQueueTag {
 public:
  TestTag() : tag_(0) {}
  TestTag(gpr_intptr tag) : tag_(tag) {}
  bool FinalizeResult(void** tag, bool* status) { return true; }
  gpr_intptr tag() { return tag_; }

 private:
  gpr_intptr tag_;
};

TEST(AlarmTest, RegularExpiry) {
  CompletionQueue cq;
  TestTag input_tag(1618033);
  Alarm alarm(&cq, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1), &input_tag);

  TestTag* output_tag;
  bool ok;
  const CompletionQueue::NextStatus status = cq.AsyncNext(
      (void**)&output_tag, &ok, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(2));

  EXPECT_EQ(status, CompletionQueue::GOT_EVENT);
  EXPECT_TRUE(ok);
  EXPECT_EQ(output_tag->tag(), input_tag.tag());
}

TEST(AlarmTest, Cancellation) {
  CompletionQueue cq;
  TestTag input_tag(1618033);
  Alarm alarm(&cq, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(2), &input_tag);
  alarm.Cancel();

  TestTag* output_tag;
  bool ok;
  const CompletionQueue::NextStatus status = cq.AsyncNext(
      (void**)&output_tag, &ok, GRPC_TIMEOUT_SECONDS_TO_DEADLINE(1));

  EXPECT_EQ(status, CompletionQueue::GOT_EVENT);
  EXPECT_FALSE(ok);
  EXPECT_EQ(output_tag->tag(), input_tag.tag());
}

}  // namespace
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
