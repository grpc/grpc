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

#include <grpc/impl/codegen/port_platform.h>

#include "src/core/ext/transport/cronet/transport/cronet_status.h"

const char* cronet_net_error_as_string(cronet_net_error_code net_error) {
  switch (net_error) {
    case OK:
      return "OK";
#define NET_ERROR(label, value)  \
  case CRONET_NET_ERROR_##label: \
    return #label;
#include "third_party/objective_c/Cronet/net_error_list.h"
#undef NET_ERROR
  }
  return "UNAVAILABLE.";
}
