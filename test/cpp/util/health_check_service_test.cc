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

#include "test/core/util/test_config.h"
#include <grpc++/health_check_service.h>
#include <grpc++/server_context.h>
#include <grpc++/status.h>
#include <gtest/gtest.h>

#include <grpc/grpc.h>

using grpc::health::v1alpha::HealthCheckRequest;
using grpc::health::v1alpha::HealthCheckResponse;

namespace grpc {
namespace testing {

class HealthCheckServiceTest : public ::testing::Test {
 protected:
  ::grpc::health::HealthCheckService service_;

  void ExpectStatus(const grpc::string& service, const Status& status,
                    const HealthCheckResponse::ServingStatus serving_status) {
    HealthCheckRequest request;
    HealthCheckResponse response;
    request.set_service(service);
    Status s = service_.Check(nullptr, &request, &response);
    EXPECT_EQ(status.error_code(), s.error_code());
    EXPECT_EQ(status.error_message(), s.error_message());
    EXPECT_EQ(serving_status, response.status());
  }
};

TEST_F(HealthCheckServiceTest, SimpleServiceTest) {
  const grpc::string kGeneralService;
  const grpc::string kServingService("grpc.package.TestService");
  const grpc::string kNonServingService("grpc.package.TestService2");
  const grpc::string kNonExistService("grpc.package.NoSuchService");

  service_.SetServingStatus(kGeneralService, HealthCheckResponse::SERVING);
  service_.SetServingStatus(kServingService, HealthCheckResponse::SERVING);
  service_.SetServingStatus(kNonServingService,
                            HealthCheckResponse::NOT_SERVING);

  ExpectStatus(kGeneralService, Status::OK, HealthCheckResponse::SERVING);
  ExpectStatus(kServingService, Status::OK, HealthCheckResponse::SERVING);
  ExpectStatus(kNonServingService, Status::OK,
               HealthCheckResponse::NOT_SERVING);
  ExpectStatus(kNonExistService, Status(StatusCode::NOT_FOUND, ""),
               HealthCheckResponse::UNKNOWN);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
