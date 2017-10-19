/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/transport/pid_controller.h"

#include <float.h>
#include <math.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include <gtest/gtest.h>
#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {

TEST(PidController, NoOp) {
  PidController pid(PidController::Args()
                        .set_gain_p(1)
                        .set_gain_i(1)
                        .set_gain_d(1)
                        .set_initial_control_value(1));
}

struct SimpleConvergenceTestArgs {
  double gain_p;
  double gain_i;
  double gain_d;
  double dt;
  double set_point;
  double start;
};

std::ostream& operator<<(std::ostream& out, SimpleConvergenceTestArgs args) {
  return out << "gain_p:" << args.gain_p << " gain_i:" << args.gain_i
             << " gain_d:" << args.gain_d << " dt:" << args.dt
             << " set_point:" << args.set_point << " start:" << args.start;
}

class SimpleConvergenceTest
    : public ::testing::TestWithParam<SimpleConvergenceTestArgs> {};

TEST_P(SimpleConvergenceTest, Converges) {
  PidController pid(PidController::Args()
                        .set_gain_p(GetParam().gain_p)
                        .set_gain_i(GetParam().gain_i)
                        .set_gain_d(GetParam().gain_d)
                        .set_initial_control_value(GetParam().start));

  for (int i = 0; i < 100000; i++) {
    pid.Update(GetParam().set_point - pid.last_control_value(), GetParam().dt);
  }

  EXPECT_LT(fabs(GetParam().set_point - pid.last_control_value()), 0.1);
  if (GetParam().gain_i > 0) {
    EXPECT_LT(fabs(pid.error_integral()), 0.1);
  }
}

INSTANTIATE_TEST_CASE_P(
    X, SimpleConvergenceTest,
    ::testing::Values(SimpleConvergenceTestArgs{0.2, 0, 0, 1, 100, 0},
                      SimpleConvergenceTestArgs{0.2, 0.1, 0, 1, 100, 0},
                      SimpleConvergenceTestArgs{0.2, 0.1, 0.1, 1, 100, 0}));

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
