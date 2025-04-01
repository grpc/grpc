//
//
// Copyright 2015 gRPC authors.
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

#include "src/core/lib/transport/status_conversion.h"

#include <grpc/support/time.h>

#include <memory>

#include "gtest/gtest.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/test_util/test_config.h"

using grpc_core::http2::Http2ErrorCode;

#define GRPC_STATUS_TO_HTTP2_ERROR(a, b) \
  ASSERT_EQ(grpc_status_to_http2_error(a), (b))
#define HTTP2_ERROR_TO_GRPC_STATUS(a, deadline, b)                \
  do {                                                            \
    grpc_core::ExecCtx exec_ctx;                                  \
    ASSERT_EQ(grpc_http2_error_to_grpc_status(a, deadline), (b)); \
                                                                  \
  } while (0)
#define GRPC_STATUS_TO_HTTP2_STATUS(a, b) \
  ASSERT_EQ(grpc_status_to_http2_status(a), (b))
#define HTTP2_STATUS_TO_GRPC_STATUS(a, b) \
  ASSERT_EQ(grpc_http2_status_to_grpc_status(a), (b))

TEST(StatusConversionTest, TestGrpcStatusToHttp2Error) {
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_OK, Http2ErrorCode::kNoError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_CANCELLED, Http2ErrorCode::kCancel);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_UNKNOWN,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_INVALID_ARGUMENT,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_DEADLINE_EXCEEDED,
                             Http2ErrorCode::kCancel);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_NOT_FOUND,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_ALREADY_EXISTS,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_PERMISSION_DENIED,
                             Http2ErrorCode::kInadequateSecurity);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_UNAUTHENTICATED,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_RESOURCE_EXHAUSTED,
                             Http2ErrorCode::kEnhanceYourCalm);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_FAILED_PRECONDITION,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_ABORTED,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_OUT_OF_RANGE,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_UNIMPLEMENTED,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_INTERNAL,
                             Http2ErrorCode::kInternalError);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_UNAVAILABLE,
                             Http2ErrorCode::kRefusedStream);
  GRPC_STATUS_TO_HTTP2_ERROR(GRPC_STATUS_DATA_LOSS,
                             Http2ErrorCode::kInternalError);
}

TEST(StatusConversionTest, TestGrpcStatusToHttp2Status) {
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_OK, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_CANCELLED, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_UNKNOWN, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_INVALID_ARGUMENT, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_DEADLINE_EXCEEDED, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_NOT_FOUND, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_ALREADY_EXISTS, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_PERMISSION_DENIED, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_UNAUTHENTICATED, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_RESOURCE_EXHAUSTED, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_FAILED_PRECONDITION, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_ABORTED, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_OUT_OF_RANGE, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_UNIMPLEMENTED, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_INTERNAL, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_UNAVAILABLE, 200);
  GRPC_STATUS_TO_HTTP2_STATUS(GRPC_STATUS_DATA_LOSS, 200);
}

TEST(StatusConversionTest, TestHttp2ErrorToGrpcStatus) {
  const grpc_core::Timestamp before_deadline =
      grpc_core::Timestamp::InfFuture();
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kNoError, before_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kProtocolError, before_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kInternalError, before_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kFlowControlError, before_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kSettingsTimeout, before_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kStreamClosed, before_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kFrameSizeError, before_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kRefusedStream, before_deadline,
                             GRPC_STATUS_UNAVAILABLE);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kCancel, before_deadline,
                             GRPC_STATUS_CANCELLED);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kCompressionError, before_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kConnectError, before_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kEnhanceYourCalm, before_deadline,
                             GRPC_STATUS_RESOURCE_EXHAUSTED);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kInadequateSecurity,
                             before_deadline, GRPC_STATUS_PERMISSION_DENIED);

  const grpc_core::Timestamp after_deadline;
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kNoError, after_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kProtocolError, after_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kInternalError, after_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kFlowControlError, after_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kSettingsTimeout, after_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kStreamClosed, after_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kFrameSizeError, after_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kRefusedStream, after_deadline,
                             GRPC_STATUS_UNAVAILABLE);
  // We only have millisecond granularity in our timing code. This sleeps for 5
  // millis to ensure that the status conversion code will pick up the fact
  // that the deadline has expired.
  gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_REALTIME),
                               gpr_time_from_millis(5, GPR_TIMESPAN)));
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kCancel, after_deadline,
                             GRPC_STATUS_DEADLINE_EXCEEDED);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kCompressionError, after_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kConnectError, after_deadline,
                             GRPC_STATUS_INTERNAL);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kEnhanceYourCalm, after_deadline,
                             GRPC_STATUS_RESOURCE_EXHAUSTED);
  HTTP2_ERROR_TO_GRPC_STATUS(Http2ErrorCode::kInadequateSecurity,
                             after_deadline, GRPC_STATUS_PERMISSION_DENIED);
}

TEST(StatusConversionTest, TestHttp2StatusToGrpcStatus) {
  HTTP2_STATUS_TO_GRPC_STATUS(200, GRPC_STATUS_OK);
  HTTP2_STATUS_TO_GRPC_STATUS(400, GRPC_STATUS_INTERNAL);
  HTTP2_STATUS_TO_GRPC_STATUS(401, GRPC_STATUS_UNAUTHENTICATED);
  HTTP2_STATUS_TO_GRPC_STATUS(403, GRPC_STATUS_PERMISSION_DENIED);
  HTTP2_STATUS_TO_GRPC_STATUS(404, GRPC_STATUS_UNIMPLEMENTED);
  HTTP2_STATUS_TO_GRPC_STATUS(409, GRPC_STATUS_UNKNOWN);
  HTTP2_STATUS_TO_GRPC_STATUS(412, GRPC_STATUS_UNKNOWN);
  HTTP2_STATUS_TO_GRPC_STATUS(429, GRPC_STATUS_UNAVAILABLE);
  HTTP2_STATUS_TO_GRPC_STATUS(499, GRPC_STATUS_UNKNOWN);
  HTTP2_STATUS_TO_GRPC_STATUS(500, GRPC_STATUS_UNKNOWN);
  HTTP2_STATUS_TO_GRPC_STATUS(502, GRPC_STATUS_UNAVAILABLE);
  HTTP2_STATUS_TO_GRPC_STATUS(503, GRPC_STATUS_UNAVAILABLE);
  HTTP2_STATUS_TO_GRPC_STATUS(504, GRPC_STATUS_UNAVAILABLE);
}

TEST(StatusConversionTest, TestGrpcHttp2StatusToGrpcStatusAll) {
  // check all status values can be converted
  for (int i = 0; i <= 999; i++) {
    grpc_http2_status_to_grpc_status(i);
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
