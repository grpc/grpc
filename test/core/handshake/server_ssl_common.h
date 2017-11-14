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

#ifndef GRPC_SERVER_SSL_COMMON_H
#define GRPC_SERVER_SSL_COMMON_H

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include "src/core/lib/iomgr/load_file.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

bool server_ssl_test(const char* alpn_list[], unsigned int alpn_list_len,
                     const char* alpn_expected);

#endif  // GRPC_SERVER_SSL_COMMON_H
