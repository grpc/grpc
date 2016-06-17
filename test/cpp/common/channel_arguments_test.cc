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
  ChannelArgumentsTest()
      : pointer_vtable_({&ChannelArguments::PointerVtableMembers::Copy,
                         &ChannelArguments::PointerVtableMembers::Destroy,
                         &ChannelArguments::PointerVtableMembers::Compare}) {}

  void SetChannelArgs(const ChannelArguments& channel_args,
                      grpc_channel_args* args) {
    channel_args.SetChannelArgs(args);
  }

  grpc::string GetDefaultUserAgentPrefix() {
    std::ostringstream user_agent_prefix;
    user_agent_prefix << "grpc-c++/" << grpc_version_string();
    return user_agent_prefix.str();
  }

  void VerifyDefaultChannelArgs() {
    grpc_channel_args args;
    SetChannelArgs(channel_args_, &args);
    EXPECT_EQ(static_cast<size_t>(1), args.num_args);
    EXPECT_STREQ(GRPC_ARG_PRIMARY_USER_AGENT_STRING, args.args[0].key);
    EXPECT_EQ(GetDefaultUserAgentPrefix(),
              grpc::string(args.args[0].value.string));
  }

  bool HasArg(grpc_arg expected_arg) {
    grpc_channel_args args;
    SetChannelArgs(channel_args_, &args);
    for (size_t i = 0; i < args.num_args; i++) {
      const grpc_arg& arg = args.args[i];
      if (arg.type == expected_arg.type &&
          grpc::string(arg.key) == expected_arg.key) {
        if (arg.type == GRPC_ARG_INTEGER) {
          return arg.value.integer == expected_arg.value.integer;
        } else if (arg.type == GRPC_ARG_STRING) {
          return grpc::string(arg.value.string) == expected_arg.value.string;
        } else if (arg.type == GRPC_ARG_POINTER) {
          return arg.value.pointer.p == expected_arg.value.pointer.p &&
                 arg.value.pointer.vtable->copy ==
                     expected_arg.value.pointer.vtable->copy &&
                 arg.value.pointer.vtable->destroy ==
                     expected_arg.value.pointer.vtable->destroy;
        }
      }
    }
    return false;
  }
  grpc_arg_pointer_vtable pointer_vtable_;
  ChannelArguments channel_args_;
};

TEST_F(ChannelArgumentsTest, SetInt) {
  VerifyDefaultChannelArgs();
  grpc::string key0("key0");
  grpc_arg arg0;
  arg0.type = GRPC_ARG_INTEGER;
  arg0.key = const_cast<char*>(key0.c_str());
  arg0.value.integer = 0;
  grpc::string key1("key1");
  grpc_arg arg1;
  arg1.type = GRPC_ARG_INTEGER;
  arg1.key = const_cast<char*>(key1.c_str());
  arg1.value.integer = 1;

  grpc::string arg_key0(key0);
  channel_args_.SetInt(arg_key0, arg0.value.integer);
  // Clear key early to make sure channel_args takes a copy
  arg_key0.clear();
  EXPECT_TRUE(HasArg(arg0));

  grpc::string arg_key1(key1);
  channel_args_.SetInt(arg_key1, arg1.value.integer);
  arg_key1.clear();
  EXPECT_TRUE(HasArg(arg0));
  EXPECT_TRUE(HasArg(arg1));
}

TEST_F(ChannelArgumentsTest, SetString) {
  VerifyDefaultChannelArgs();
  grpc::string key0("key0");
  grpc::string val0("val0");
  grpc_arg arg0;
  arg0.type = GRPC_ARG_STRING;
  arg0.key = const_cast<char*>(key0.c_str());
  arg0.value.string = const_cast<char*>(val0.c_str());
  grpc::string key1("key1");
  grpc::string val1("val1");
  grpc_arg arg1;
  arg1.type = GRPC_ARG_STRING;
  arg1.key = const_cast<char*>(key1.c_str());
  arg1.value.string = const_cast<char*>(val1.c_str());

  grpc::string key(key0);
  grpc::string val(val0);
  channel_args_.SetString(key, val);
  // Clear key/val early to make sure channel_args takes a copy
  key = "";
  val = "";
  EXPECT_TRUE(HasArg(arg0));

  key = key1;
  val = val1;
  channel_args_.SetString(key, val);
  // Clear key/val early to make sure channel_args takes a copy
  key = "";
  val = "";
  EXPECT_TRUE(HasArg(arg0));
  EXPECT_TRUE(HasArg(arg1));
}

TEST_F(ChannelArgumentsTest, SetPointer) {
  VerifyDefaultChannelArgs();
  grpc::string key0("key0");
  grpc_arg arg0;
  arg0.type = GRPC_ARG_POINTER;
  arg0.key = const_cast<char*>(key0.c_str());
  arg0.value.pointer.p = &key0;
  arg0.value.pointer.vtable = &pointer_vtable_;

  grpc::string key(key0);
  channel_args_.SetPointer(key, arg0.value.pointer.p);
  EXPECT_TRUE(HasArg(arg0));
}

TEST_F(ChannelArgumentsTest, SetUserAgentPrefix) {
  VerifyDefaultChannelArgs();
  grpc::string prefix("prefix");
  grpc::string whole_prefix = prefix + " " + GetDefaultUserAgentPrefix();
  grpc_arg arg0;
  arg0.type = GRPC_ARG_STRING;
  arg0.key = const_cast<char*>(GRPC_ARG_PRIMARY_USER_AGENT_STRING);
  arg0.value.string = const_cast<char*>(whole_prefix.c_str());

  channel_args_.SetUserAgentPrefix(prefix);
  EXPECT_TRUE(HasArg(arg0));
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
