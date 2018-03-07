/*
 *
 * Copyright 2018 gRPC authors.
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

#include <memory>

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>

#include <grpc/grpc.h>
#include <gtest/gtest.h>

#include "src/core/lib/channel/channel_trace.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/proto/grpc/channelz/channelz.pb.h"

namespace grpc {
namespace testing {

using grpc_core::ChannelTrace;
using grpc_core::MakeRefCounted;
using grpc_core::RefCountedPtr;

namespace {

void AddSimpleTrace(RefCountedPtr<ChannelTrace> tracer) {
  tracer->AddTraceEvent(grpc_slice_from_static_string("simple trace"),
                        GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                        GRPC_CHANNEL_READY);
}

}  // namespace

TEST(ChannelTraceTest, ProtoJsonTest) {
  grpc_core::ExecCtx exec_ctx;
  RefCountedPtr<ChannelTrace> tracer = MakeRefCounted<ChannelTrace>(10);
  AddSimpleTrace(tracer);
  AddSimpleTrace(tracer);
  tracer->AddTraceEvent(
      grpc_slice_from_static_string("trace three"),
      grpc_error_set_int(GRPC_ERROR_CREATE_FROM_STATIC_STRING("Error"),
                         GRPC_ERROR_INT_HTTP2_ERROR, 2),
      GRPC_CHANNEL_IDLE);
  tracer->AddTraceEvent(grpc_slice_from_static_string("trace four"),
                        GRPC_ERROR_NONE, GRPC_CHANNEL_SHUTDOWN);
  std::string json_str = tracer->RenderTrace();
  gpr_log(GPR_ERROR, "%s", json_str.c_str());
  grpc::channelz::ChannelTrace channel_trace;
  google::protobuf::util::JsonParseOptions options;
  options.ignore_unknown_fields = true;
  ASSERT_EQ(google::protobuf::util::JsonStringToMessage(
                json_str, &channel_trace, options),
            google::protobuf::util::Status::OK);
  std::string str;
  google::protobuf::TextFormat::PrintToString(channel_trace, &str);
  gpr_log(GPR_ERROR, "%s", str.c_str());
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_init();
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
