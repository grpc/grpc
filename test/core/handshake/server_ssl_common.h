//
// Created by apolcyn on 10/31/17.
//

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
