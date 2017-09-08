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

#include <grpc/grpc.h>

extern void grpc_http_filters_init(void);
extern void grpc_http_filters_shutdown(void);
extern void grpc_chttp2_plugin_init(void);
extern void grpc_chttp2_plugin_shutdown(void);
extern void grpc_deadline_filter_init(void);
extern void grpc_deadline_filter_shutdown(void);
extern void grpc_client_channel_init(void);
extern void grpc_client_channel_shutdown(void);
extern void grpc_tsi_gts_init(void);
extern void grpc_tsi_gts_shutdown(void);
extern void grpc_server_load_reporting_plugin_init(void);
extern void grpc_server_load_reporting_plugin_shutdown(void);

void grpc_register_built_in_plugins(void) {
  grpc_register_plugin(grpc_http_filters_init,
                       grpc_http_filters_shutdown);
  grpc_register_plugin(grpc_chttp2_plugin_init,
                       grpc_chttp2_plugin_shutdown);
  grpc_register_plugin(grpc_deadline_filter_init,
                       grpc_deadline_filter_shutdown);
  grpc_register_plugin(grpc_client_channel_init,
                       grpc_client_channel_shutdown);
  grpc_register_plugin(grpc_tsi_gts_init,
                       grpc_tsi_gts_shutdown);
  grpc_register_plugin(grpc_server_load_reporting_plugin_init,
                       grpc_server_load_reporting_plugin_shutdown);
}
