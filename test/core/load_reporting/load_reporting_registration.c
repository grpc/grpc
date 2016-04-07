/*
 *
 * Copyright 2015-2016, Google Inc.
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
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/load_reporting/load_reporting.h"
#include "src/core/lib/surface/api_trace.h"
#include "test/core/util/test_config.h"

static void noop(load_reporting_data *lr_data) {
  uint32_t *d = (uint32_t *)(lr_data->data);
  *d = 0xdeadbeef;
}

static void lr_plugin_init(void) { grpc_load_reporting_init(noop); }

static void lr_plugin_destroy(void) {}

static void load_reporting_register() {
  grpc_register_plugin(lr_plugin_init, lr_plugin_destroy);
}

static void test_load_reporter_registration(void) {
  load_reporting_data lr_data;
  lr_data.data = gpr_malloc(sizeof(uint32_t));
  grpc_load_reporting_call(&lr_data);

  GPR_ASSERT(*((uint32_t *)lr_data.data) == 0xdeadbeef);

  gpr_free(lr_data.data);
}

int main(int argc, char **argv) {
  load_reporting_register();
  grpc_init();
  test_load_reporter_registration();
  grpc_shutdown();

  return 0;
}
