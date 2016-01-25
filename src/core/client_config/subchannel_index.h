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

#ifndef GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_INDEX_H
#define GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_INDEX_H

#include "src/core/client_config/connector.h"
#include "src/core/client_config/subchannel.h"

typedef struct grpc_subchannel_key grpc_subchannel_key;

grpc_subchannel_key *grpc_subchannel_key_create(grpc_connector *con,
                                                grpc_subchannel_args *args);

void grpc_subchannel_key_destroy(grpc_subchannel_key *key);

grpc_subchannel *grpc_subchannel_index_find(grpc_exec_ctx *exec_ctx,
                                            grpc_subchannel_key *key);

grpc_subchannel *grpc_subchannel_index_register(grpc_exec_ctx *exec_ctx,
                                                grpc_subchannel_key *key,
                                                grpc_subchannel *constructed);

void grpc_subchannel_index_unregister(grpc_exec_ctx *exec_ctx,
                                      grpc_subchannel_key *key,
                                      grpc_subchannel *constructed);

void grpc_subchannel_index_init(void);
void grpc_subchannel_index_shutdown(void);

#endif /* GRPC_INTERNAL_CORE_CLIENT_CONFIG_SUBCHANNEL_INDEX_H */
