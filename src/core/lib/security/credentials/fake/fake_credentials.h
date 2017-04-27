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

#ifndef GRPC_CORE_LIB_SECURITY_CREDENTIALS_FAKE_FAKE_CREDENTIALS_H
#define GRPC_CORE_LIB_SECURITY_CREDENTIALS_FAKE_FAKE_CREDENTIALS_H

#include "src/core/lib/security/credentials/credentials.h"

/* -- Fake transport security credentials. -- */

/* Creates a fake transport security credentials object for testing. */
grpc_channel_credentials *grpc_fake_transport_security_credentials_create(void);

/* Creates a fake server transport security credentials object for testing. */
grpc_server_credentials *grpc_fake_transport_security_server_credentials_create(
    void);

/* Used to verify the target names given to the fake transport security
 * connector.
 *
 * The syntax of \a expected_targets by example:
 * For LB channels:
 *     "backend_target_1,backend_target_2,...;lb_target_1,lb_target_2,..."
 * For regular channels:
 *     "backend_taget_1,backend_target_2,..."
 *
 * That is to say, LB channels have a heading list of LB targets separated from
 * the list of backend targets by a semicolon. For non-LB channels, only the
 * latter is present. */
grpc_arg grpc_fake_transport_expected_targets_arg(char *expected_targets);

/* Return the value associated with the expected targets channel arg or NULL */
const char *grpc_fake_transport_get_expected_targets(
    const grpc_channel_args *args);

/* --  Metadata-only Test credentials. -- */

typedef struct {
  grpc_call_credentials base;
  grpc_credentials_md_store *md_store;
  int is_async;
} grpc_md_only_test_credentials;

#endif /* GRPC_CORE_LIB_SECURITY_CREDENTIALS_FAKE_FAKE_CREDENTIALS_H */
