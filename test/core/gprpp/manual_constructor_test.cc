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

/* Test of gpr synchronization support. */

#include "src/core/lib/gprpp/manual_constructor.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>

#include "test/core/util/test_config.h"

class A {
 public:
  A() {}
  virtual ~A() {}
  virtual const char* foo() { return "A_foo"; }
  virtual const char* bar() { return "A_bar"; }
};

class B : public A {
 public:
  B() {}
  ~B() override {}
  const char* foo() override { return "B_foo"; }
  char get_junk() { return junk[0]; }

 private:
  char junk[1000];
};

class C : public B {
 public:
  C() {}
  ~C() override {}
  const char* bar() override { return "C_bar"; }
  char get_more_junk() { return more_junk[0]; }

 private:
  char more_junk[1000];
};

class D : public A {
 public:
  const char* bar() override { return "D_bar"; }
};

static void basic_test() {
  grpc_core::PolymorphicManualConstructor<A, B> poly;
  poly.Init<B>();
  GPR_ASSERT(!strcmp(poly->foo(), "B_foo"));
  GPR_ASSERT(!strcmp(poly->bar(), "A_bar"));
}

static void complex_test() {
  grpc_core::PolymorphicManualConstructor<A, B, C, D> polyB;
  polyB.Init<B>();
  GPR_ASSERT(!strcmp(polyB->foo(), "B_foo"));
  GPR_ASSERT(!strcmp(polyB->bar(), "A_bar"));

  grpc_core::PolymorphicManualConstructor<A, B, C, D> polyC;
  polyC.Init<C>();
  GPR_ASSERT(!strcmp(polyC->foo(), "B_foo"));
  GPR_ASSERT(!strcmp(polyC->bar(), "C_bar"));

  grpc_core::PolymorphicManualConstructor<A, B, C, D> polyD;
  polyD.Init<D>();
  GPR_ASSERT(!strcmp(polyD->foo(), "A_foo"));
  GPR_ASSERT(!strcmp(polyD->bar(), "D_bar"));
}

/* ------------------------------------------------- */

int main(int argc, char* argv[]) {
  grpc::testing::TestEnvironment env(argc, argv);
  basic_test();
  complex_test();
  return 0;
}
