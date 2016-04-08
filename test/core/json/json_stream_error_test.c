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

#include <stdio.h>
#include <stdlib.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/json/json_writer.h"

static int g_string_clear_once = 0;

static void string_clear(void *userdata) {
  GPR_ASSERT(!g_string_clear_once);
  g_string_clear_once = 1;
}

static uint32_t read_char(void *userdata) { return GRPC_JSON_READ_CHAR_ERROR; }

static grpc_json_reader_vtable reader_vtable = {
    string_clear, NULL, NULL, read_char, NULL, NULL,
    NULL,         NULL, NULL, NULL,      NULL, NULL};

static void read_error() {
  grpc_json_reader reader;
  grpc_json_reader_status status;
  grpc_json_reader_init(&reader, &reader_vtable, NULL);

  status = grpc_json_reader_run(&reader);
  GPR_ASSERT(status == GRPC_JSON_READ_ERROR);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  read_error();
  gpr_log(GPR_INFO, "json_stream_error success");
  return 0;
}
