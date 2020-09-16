/*
 *
 * Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_EXT_TRANSPORT_CRONET_TRANSPORT_CRONET_STATUS_H
#define GRPC_CORE_EXT_TRANSPORT_CRONET_TRANSPORT_CRONET_STATUS_H

#include <grpc/impl/codegen/port_platform.h>

enum cronet_net_error_code {
  OK = 0,
#define NET_ERROR(label, value) CRONET_NET_ERROR_##label = value,
#include "third_party/objective_c/Cronet/net_error_list.h"
#undef NET_ERROR
};

const char* cronet_net_error_as_string(cronet_net_error_code net_error);

#endif /* GRPC_CORE_EXT_TRANSPORT_CRONET_TRANSPORT_CRONET_STATUS_H */
