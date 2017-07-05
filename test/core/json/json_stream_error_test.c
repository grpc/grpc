/*
 *
 * Copyright 2015 gRPC authors.
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
