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

#include <grpc/grpc.h>

extern void grpc_chttp2_plugin_init(void);
extern void grpc_chttp2_plugin_shutdown(void);
extern void grpc_client_config_init(void);
extern void grpc_client_config_shutdown(void);
extern void grpc_lb_policy_pick_first_init(void);
extern void grpc_lb_policy_pick_first_shutdown(void);
extern void grpc_lb_policy_round_robin_init(void);
extern void grpc_lb_policy_round_robin_shutdown(void);
extern void grpc_resolver_dns_native_init(void);
extern void grpc_resolver_dns_native_shutdown(void);
extern void grpc_resolver_sockaddr_init(void);
extern void grpc_resolver_sockaddr_shutdown(void);
extern void grpc_load_reporting_plugin_init(void);
extern void grpc_load_reporting_plugin_shutdown(void);
extern void census_grpc_plugin_init(void);
extern void census_grpc_plugin_shutdown(void);

void grpc_register_built_in_plugins(void) {
  grpc_register_plugin(grpc_chttp2_plugin_init,
                       grpc_chttp2_plugin_shutdown);
  grpc_register_plugin(grpc_client_config_init,
                       grpc_client_config_shutdown);
  grpc_register_plugin(grpc_lb_policy_pick_first_init,
                       grpc_lb_policy_pick_first_shutdown);
  grpc_register_plugin(grpc_lb_policy_round_robin_init,
                       grpc_lb_policy_round_robin_shutdown);
  grpc_register_plugin(grpc_resolver_dns_native_init,
                       grpc_resolver_dns_native_shutdown);
  grpc_register_plugin(grpc_resolver_sockaddr_init,
                       grpc_resolver_sockaddr_shutdown);
  grpc_register_plugin(grpc_load_reporting_plugin_init,
                       grpc_load_reporting_plugin_shutdown);
  grpc_register_plugin(census_grpc_plugin_init,
                       census_grpc_plugin_shutdown);
}
