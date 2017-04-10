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

#include <sys/resource.h>

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "test/core/util/test_config.h"

int main(int argc, char **argv) {
  int i;
  struct rlimit rlim;
  grpc_endpoint_pair p;

  grpc_test_init(argc, argv);
  grpc_iomgr_init();

  /* set max # of file descriptors to a low value, and
     verify we can create and destroy many more than this number
     of descriptors */
  rlim.rlim_cur = rlim.rlim_max = 10;
  GPR_ASSERT(0 == setrlimit(RLIMIT_NOFILE, &rlim));
  grpc_resource_quota *resource_quota =
      grpc_resource_quota_create("fd_conservation_posix_test");

  for (i = 0; i < 100; i++) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    p = grpc_iomgr_create_endpoint_pair("test", NULL);
    grpc_endpoint_destroy(&exec_ctx, p.client);
    grpc_endpoint_destroy(&exec_ctx, p.server);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  grpc_resource_quota_unref(resource_quota);

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_iomgr_shutdown(&exec_ctx);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  return 0;
}
