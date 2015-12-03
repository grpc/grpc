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

#include <grpc++/support/channel_arguments.h>

#include <grpc/grpc.h>
#include <gtest/gtest.h>

namespace grpc {
namespace testing {

class ChannelArgumentsTest : public ::testing::Test {
 protected:
  void SetChannelArgs(const ChannelArguments& channel_args,
                      grpc_channel_args* args) {
    channel_args.SetChannelArgs(args);
  }
};

TEST_F(ChannelArgumentsTest, SetInt) {
  grpc_channel_args args;
  ChannelArguments channel_args;
  // Empty arguments.
  SetChannelArgs(channel_args, &args);
  EXPECT_EQ(static_cast<size_t>(0), args.num_args);

  grpc::string key("key0");
  channel_args.SetInt(key, 0);
  // Clear key early to make sure channel_args takes a copy
  key = "";
  SetChannelArgs(channel_args, &args);
  EXPECT_EQ(static_cast<size_t>(1), args.num_args);
  EXPECT_EQ(GRPC_ARG_INTEGER, args.args[0].type);
  EXPECT_STREQ("key0", args.args[0].key);
  EXPECT_EQ(0, args.args[0].value.integer);

  key = "key1";
  channel_args.SetInt(key, 1);
  key = "";
  SetChannelArgs(channel_args, &args);
  EXPECT_EQ(static_cast<size_t>(2), args.num_args);
  // We do not enforce order on the arguments.
  for (size_t i = 0; i < args.num_args; i++) {
    EXPECT_EQ(GRPC_ARG_INTEGER, args.args[i].type);
    if (grpc::string(args.args[i].key) == "key0") {
      EXPECT_EQ(0, args.args[i].value.integer);
    } else if (grpc::string(args.args[i].key) == "key1") {
      EXPECT_EQ(1, args.args[i].value.integer);
    }
  }
}

TEST_F(ChannelArgumentsTest, SetString) {
  grpc_channel_args args;
  ChannelArguments channel_args;
  // Empty arguments.
  SetChannelArgs(channel_args, &args);
  EXPECT_EQ(static_cast<size_t>(0), args.num_args);

  grpc::string key("key0");
  grpc::string val("val0");
  channel_args.SetString(key, val);
  // Clear key/val early to make sure channel_args takes a copy
  key = "";
  val = "";
  SetChannelArgs(channel_args, &args);
  EXPECT_EQ(static_cast<size_t>(1), args.num_args);
  EXPECT_EQ(GRPC_ARG_STRING, args.args[0].type);
  EXPECT_STREQ("key0", args.args[0].key);
  EXPECT_STREQ("val0", args.args[0].value.string);

  key = "key1";
  val = "val1";
  channel_args.SetString(key, val);
  SetChannelArgs(channel_args, &args);
  EXPECT_EQ(static_cast<size_t>(2), args.num_args);
  // We do not enforce order on the arguments.
  for (size_t i = 0; i < args.num_args; i++) {
    EXPECT_EQ(GRPC_ARG_STRING, args.args[i].type);
    if (grpc::string(args.args[i].key) == "key0") {
      EXPECT_STREQ("val0", args.args[i].value.string);
    } else if (grpc::string(args.args[i].key) == "key1") {
      EXPECT_STREQ("val1", args.args[i].value.string);
    }
  }
}

TEST_F(ChannelArgumentsTest, SetPointer) {
  grpc_channel_args args;
  ChannelArguments channel_args;
  // Empty arguments.
  SetChannelArgs(channel_args, &args);
  EXPECT_EQ(static_cast<size_t>(0), args.num_args);

  grpc::string key("key0");
  channel_args.SetPointer(key, &key);
  SetChannelArgs(channel_args, &args);
  EXPECT_EQ(static_cast<size_t>(1), args.num_args);
  EXPECT_EQ(GRPC_ARG_POINTER, args.args[0].type);
  EXPECT_STREQ("key0", args.args[0].key);
  EXPECT_EQ(&key, args.args[0].value.pointer.p);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
