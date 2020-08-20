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

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

void grpc_http_filters_init(void);
void grpc_http_filters_shutdown(void);
void grpc_chttp2_plugin_init(void);
void grpc_chttp2_plugin_shutdown(void);
void grpc_deadline_filter_init(void);
void grpc_deadline_filter_shutdown(void);
void grpc_client_channel_init(void);
void grpc_client_channel_shutdown(void);
void grpc_inproc_plugin_init(void);
void grpc_inproc_plugin_shutdown(void);
void grpc_resolver_fake_init(void);
void grpc_resolver_fake_shutdown(void);
void grpc_lb_policy_grpclb_init(void);
void grpc_lb_policy_grpclb_shutdown(void);
void grpc_lb_policy_cds_init(void);
void grpc_lb_policy_cds_shutdown(void);
void grpc_lb_policy_eds_init(void);
void grpc_lb_policy_eds_shutdown(void);
void grpc_lb_policy_lrs_init(void);
void grpc_lb_policy_lrs_shutdown(void);
void grpc_lb_policy_priority_init(void);
void grpc_lb_policy_priority_shutdown(void);
void grpc_lb_policy_weighted_target_init(void);
void grpc_lb_policy_weighted_target_shutdown(void);
void grpc_lb_policy_xds_routing_init(void);
void grpc_lb_policy_xds_routing_shutdown(void);
void grpc_lb_policy_pick_first_init(void);
void grpc_lb_policy_pick_first_shutdown(void);
void grpc_lb_policy_round_robin_init(void);
void grpc_lb_policy_round_robin_shutdown(void);
void grpc_resolver_dns_ares_init(void);
void grpc_resolver_dns_ares_shutdown(void);
void grpc_resolver_dns_native_init(void);
void grpc_resolver_dns_native_shutdown(void);
void grpc_resolver_sockaddr_init(void);
void grpc_resolver_sockaddr_shutdown(void);
void grpc_resolver_xds_init(void);
void grpc_resolver_xds_shutdown(void);
void grpc_client_idle_filter_init(void);
void grpc_client_idle_filter_shutdown(void);
void grpc_max_age_filter_init(void);
void grpc_max_age_filter_shutdown(void);
void grpc_message_size_filter_init(void);
void grpc_message_size_filter_shutdown(void);
void grpc_service_config_channel_arg_filter_init(void);
void grpc_service_config_channel_arg_filter_shutdown(void);
void grpc_client_authority_filter_init(void);
void grpc_client_authority_filter_shutdown(void);
void grpc_workaround_cronet_compression_filter_init(void);
void grpc_workaround_cronet_compression_filter_shutdown(void);

void grpc_register_built_in_plugins(void) {
  grpc_register_plugin(grpc_http_filters_init,
                       grpc_http_filters_shutdown);
  grpc_register_plugin(grpc_chttp2_plugin_init,
                       grpc_chttp2_plugin_shutdown);
  grpc_register_plugin(grpc_deadline_filter_init,
                       grpc_deadline_filter_shutdown);
  grpc_register_plugin(grpc_client_channel_init,
                       grpc_client_channel_shutdown);
  grpc_register_plugin(grpc_inproc_plugin_init,
                       grpc_inproc_plugin_shutdown);
  grpc_register_plugin(grpc_resolver_fake_init,
                       grpc_resolver_fake_shutdown);
  grpc_register_plugin(grpc_lb_policy_grpclb_init,
                       grpc_lb_policy_grpclb_shutdown);
  grpc_register_plugin(grpc_lb_policy_cds_init,
                       grpc_lb_policy_cds_shutdown);
  grpc_register_plugin(grpc_lb_policy_eds_init,
                       grpc_lb_policy_eds_shutdown);
  grpc_register_plugin(grpc_lb_policy_lrs_init,
                       grpc_lb_policy_lrs_shutdown);
  grpc_register_plugin(grpc_lb_policy_priority_init,
                       grpc_lb_policy_priority_shutdown);
  grpc_register_plugin(grpc_lb_policy_weighted_target_init,
                       grpc_lb_policy_weighted_target_shutdown);
  grpc_register_plugin(grpc_lb_policy_xds_routing_init,
                       grpc_lb_policy_xds_routing_shutdown);
  grpc_register_plugin(grpc_lb_policy_pick_first_init,
                       grpc_lb_policy_pick_first_shutdown);
  grpc_register_plugin(grpc_lb_policy_round_robin_init,
                       grpc_lb_policy_round_robin_shutdown);
  grpc_register_plugin(grpc_resolver_dns_ares_init,
                       grpc_resolver_dns_ares_shutdown);
  grpc_register_plugin(grpc_resolver_dns_native_init,
                       grpc_resolver_dns_native_shutdown);
  grpc_register_plugin(grpc_resolver_sockaddr_init,
                       grpc_resolver_sockaddr_shutdown);
  grpc_register_plugin(grpc_resolver_xds_init,
                       grpc_resolver_xds_shutdown);
  grpc_register_plugin(grpc_client_idle_filter_init,
                       grpc_client_idle_filter_shutdown);
  grpc_register_plugin(grpc_max_age_filter_init,
                       grpc_max_age_filter_shutdown);
  grpc_register_plugin(grpc_message_size_filter_init,
                       grpc_message_size_filter_shutdown);
  grpc_register_plugin(grpc_service_config_channel_arg_filter_init,
                       grpc_service_config_channel_arg_filter_shutdown);
  grpc_register_plugin(grpc_client_authority_filter_init,
                       grpc_client_authority_filter_shutdown);
  grpc_register_plugin(grpc_workaround_cronet_compression_filter_init,
                       grpc_workaround_cronet_compression_filter_shutdown);
}
