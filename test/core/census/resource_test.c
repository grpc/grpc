/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/ext/census/resource.h"
#include <grpc/census.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "src/core/ext/census/base_resources.h"
#include "test/core/util/test_config.h"

// Test all the functionality for dealing with Resources.

// Just startup and shutdown resources subsystem.
static void test_enable_disable() {
  initialize_resources();
  shutdown_resources();
}

// A blank/empty initialization should not work.
static void test_empty_definition() {
  initialize_resources();
  int32_t rid = census_define_resource(NULL, 0);
  GPR_ASSERT(rid == -1);
  uint8_t buffer[50] = {0};
  rid = census_define_resource(buffer, 50);
  GPR_ASSERT(rid == -1);
  shutdown_resources();
}

// Given a file name, read raw proto and define the resource included within.
// Returns resource id from census_define_resource().
static int32_t define_resource_from_file(const char *file) {
#define BUF_SIZE 512
  uint8_t buffer[BUF_SIZE];
  FILE *input = fopen(file, "rb");
  GPR_ASSERT(input != NULL);
  size_t nbytes = fread(buffer, 1, BUF_SIZE, input);
  GPR_ASSERT(nbytes != 0 && nbytes < BUF_SIZE && feof(input) && !ferror(input));
  int32_t rid = census_define_resource(buffer, nbytes);
  GPR_ASSERT(fclose(input) == 0);
  return rid;
}

// Test definition of a single resource, using a proto read from a file. The
// `succeed` parameter indicates whether we expect the definition to succeed or
// fail. `name` is used to check that the returned resource can be looked up by
// name.
static void test_define_single_resource(const char *file, const char *name,
                                        bool succeed) {
  gpr_log(GPR_INFO, "Test defining resource \"%s\"\n", name);
  initialize_resources();
  int32_t rid = define_resource_from_file(file);
  if (succeed) {
    GPR_ASSERT(rid >= 0);
    int32_t rid2 = census_resource_id(name);
    GPR_ASSERT(rid == rid2);
  } else {
    GPR_ASSERT(rid < 0);
  }
  shutdown_resources();
}

// Try deleting various resources (both those that exist and those that don't).
static void test_delete_resource(const char *minimal_good, const char *full) {
  initialize_resources();
  // Try deleting resource before any are defined.
  census_delete_resource(0);
  // Create and check a couple of resources.
  int32_t rid1 = define_resource_from_file(minimal_good);
  int32_t rid2 = define_resource_from_file(full);
  GPR_ASSERT(rid1 >= 0 && rid2 >= 0 && rid1 != rid2);
  int32_t rid3 = census_resource_id("minimal_good");
  int32_t rid4 = census_resource_id("full_resource");
  GPR_ASSERT(rid1 == rid3 && rid2 == rid4);
  // Try deleting non-existant resources.
  census_delete_resource(-1);
  census_delete_resource(rid1 + rid2 + 1);
  census_delete_resource(10000000);
  // Delete one of the previously defined resources and check for deletion.
  census_delete_resource(rid1);
  rid3 = census_resource_id("minimal_good");
  GPR_ASSERT(rid3 < 0);
  // Check that re-adding works.
  rid1 = define_resource_from_file(minimal_good);
  GPR_ASSERT(rid1 >= 0);
  rid3 = census_resource_id("minimal_good");
  GPR_ASSERT(rid1 == rid3);
  shutdown_resources();
}

// Test define base resources.
static void test_base_resources() {
  initialize_resources();
  define_base_resources();
  int32_t rid1 = census_resource_id("client_rpc_latency");
  int32_t rid2 = census_resource_id("server_rpc_latency");
  GPR_ASSERT(rid1 >= 0 && rid2 >= 0 && rid1 != rid2);
  shutdown_resources();
}

int main(int argc, char **argv) {
  const char *resource_empty_name_pb, *resource_full_pb,
      *resource_minimal_good_pb, *resource_no_name_pb,
      *resource_no_numerator_pb, *resource_no_unit_pb;
  if (argc == 7) {
    resource_empty_name_pb = argv[1];
    resource_full_pb = argv[2];
    resource_minimal_good_pb = argv[3];
    resource_no_name_pb = argv[4];
    resource_no_numerator_pb = argv[5];
    resource_no_unit_pb = argv[6];
  } else {
    GPR_ASSERT(argc == 1);
    resource_empty_name_pb = "test/core/census/data/resource_empty_name.pb";
    resource_full_pb = "test/core/census/data/resource_full.pb";
    resource_minimal_good_pb = "test/core/census/data/resource_minimal_good.pb";
    resource_no_name_pb = "test/core/census/data/resource_no_name.pb";
    resource_no_numerator_pb = "test/core/census/data/resource_no_numerator.pb";
    resource_no_unit_pb = "test/core/census/data/resource_no_unit.pb";
  }
  grpc_test_init(argc, argv);
  test_enable_disable();
  test_empty_definition();
  test_define_single_resource(resource_minimal_good_pb, "minimal_good", true);
  test_define_single_resource(resource_full_pb, "full_resource", true);
  test_define_single_resource(resource_no_name_pb, "resource_no_name", false);
  test_define_single_resource(resource_no_numerator_pb, "resource_no_numerator",
                              false);
  test_define_single_resource(resource_no_unit_pb, "resource_no_unit", false);
  test_define_single_resource(resource_empty_name_pb, "resource_empty_name",
                              false);
  test_delete_resource(resource_minimal_good_pb, resource_full_pb);
  test_base_resources();
  return 0;
}
