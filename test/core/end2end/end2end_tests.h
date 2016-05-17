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

#ifndef GRPC_TEST_CORE_END2END_END2END_TESTS_H
#define GRPC_TEST_CORE_END2END_END2END_TESTS_H

#include <grpc/grpc.h>

typedef struct grpc_end2end_test_fixture grpc_end2end_test_fixture;
typedef struct grpc_end2end_test_config grpc_end2end_test_config;

#define FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION 1
#define FEATURE_MASK_SUPPORTS_HOSTNAME_VERIFICATION 2
#define FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS 4

#define FAIL_AUTH_CHECK_SERVER_ARG_NAME "fail_auth_check"

struct grpc_end2end_test_fixture {
  grpc_completion_queue *cq;
  grpc_server *server;
  grpc_channel *client;
  void *fixture_data;
};

struct grpc_end2end_test_config {
  const char *name;
  uint32_t feature_mask;
  grpc_end2end_test_fixture (*create_fixture)(grpc_channel_args *client_args,
                                              grpc_channel_args *server_args);
  void (*init_client)(grpc_end2end_test_fixture *f,
                      grpc_channel_args *client_args);
  void (*init_server)(grpc_end2end_test_fixture *f,
                      grpc_channel_args *server_args);
  void (*tear_down_data)(grpc_end2end_test_fixture *f);
};

void grpc_end2end_tests_pre_init(void);
void grpc_end2end_tests(int argc, char **argv, grpc_end2end_test_config config);

#endif /* GRPC_TEST_CORE_END2END_END2END_TESTS_H */
