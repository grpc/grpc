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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include "src/core/lib/json/json.h"
#include "src/core/lib/support/string.h"

#include "test/core/util/test_config.h"

typedef struct testing_pair {
  const char *input;
  const char *output;
} testing_pair;

static testing_pair testing_pairs[] = {
    /* Testing valid parsing. */
    /* Testing trivial parses, with de-indentation. */
    {" 0 ", "0"},
    {" 1 ", "1"},
    {" \"    \" ", "\"    \""},
    {" \"a\" ", "\"a\""},
    {" true ", "true"},
    /* Testing the parser's ability to decode trivial UTF-16. */
    {"\"\\u0020\\\\\\u0010\\u000a\\u000D\"", "\" \\\\\\u0010\\n\\r\""},
    /* Testing various UTF-8 sequences. */
    {"\"ßâñć௵⇒\"", "\"\\u00df\\u00e2\\u00f1\\u0107\\u0bf5\\u21d2\""},
    {"\"\\u00df\\u00e2\\u00f1\\u0107\\u0bf5\\u21d2\"",
     "\"\\u00df\\u00e2\\u00f1\\u0107\\u0bf5\\u21d2\""},
    /* Testing UTF-8 character "𝄞", U+11D1E. */
    {"\"\xf0\x9d\x84\x9e\"", "\"\\ud834\\udd1e\""},
    {"\"\\ud834\\udd1e\"", "\"\\ud834\\udd1e\""},
    {"{\"\\ud834\\udd1e\":0}", "{\"\\ud834\\udd1e\":0}"},
    /* Testing nested empty containers. */
    {
        " [ [ ] , { } , [ ] ] ", "[[],{},[]]",
    },
    /* Testing escapes and control chars in key strings. */
    {" { \"\\u007f\x7f\\n\\r\\\"\\f\\b\\\\a , b\": 1, \"\": 0 } ",
     "{\"\\u007f\\u007f\\n\\r\\\"\\f\\b\\\\a , b\":1,\"\":0}"},
    /* Testing the writer's ability to cut off invalid UTF-8 sequences. */
    {"\"abc\xf0\x9d\x24\"", "\"abc\""},
    {"\"\xff\"", "\"\""},
    /* Testing valid number parsing. */
    {"[0, 42 , 0.0123, 123.456]", "[0,42,0.0123,123.456]"},
    {"[1e4,-53.235e-31, 0.3e+3]", "[1e4,-53.235e-31,0.3e+3]"},
    /* Testing keywords parsing. */
    {"[true, false, null]", "[true,false,null]"},

    /* Testing invalid parsing. */

    /* Testing plain invalid things, exercising the state machine. */
    {"\\", NULL},
    {"nu ll", NULL},
    {"{\"foo\": bar}", NULL},
    {"{\"foo\": bar\"x\"}", NULL},
    {"fals", NULL},
    {"0,0 ", NULL},
    {"\"foo\",[]", NULL},
    /* Testing unterminated string. */
    {"\"\\x", NULL},
    /* Testing invalid UTF-16 number. */
    {"\"\\u123x", NULL},
    {"{\"\\u123x", NULL},
    /* Testing imbalanced surrogate pairs. */
    {"\"\\ud834f", NULL},
    {"{\"\\ud834f\":0}", NULL},
    {"\"\\ud834\\n", NULL},
    {"{\"\\ud834\\n\":0}", NULL},
    {"\"\\udd1ef", NULL},
    {"{\"\\udd1ef\":0}", NULL},
    {"\"\\ud834\\ud834\"", NULL},
    {"{\"\\ud834\\ud834\"\":0}", NULL},
    {"\"\\ud834\\u1234\"", NULL},
    {"{\"\\ud834\\u1234\"\":0}", NULL},
    {"\"\\ud834]\"", NULL},
    {"{\"\\ud834]\"\":0}", NULL},
    {"\"\\ud834 \"", NULL},
    {"{\"\\ud834 \"\":0}", NULL},
    {"\"\\ud834\\\\\"", NULL},
    {"{\"\\ud834\\\\\"\":0}", NULL},
    /* Testing embedded invalid whitechars. */
    {"\"\n\"", NULL},
    {"\"\t\"", NULL},
    /* Testing empty json data. */
    {"", NULL},
    /* Testing extra characters after end of parsing. */
    {"{},", NULL},
    /* Testing imbalanced containers. */
    {"{}}", NULL},
    {"[]]", NULL},
    {"{{}", NULL},
    {"[[]", NULL},
    {"[}", NULL},
    {"{]", NULL},
    /* Testing bad containers. */
    {"{x}", NULL},
    {"{x=0,y}", NULL},
    /* Testing trailing comma. */
    {"{,}", NULL},
    {"[1,2,3,4,]", NULL},
    {"{\"a\": 1, }", NULL},
    /* Testing after-ending characters. */
    {"{}x", NULL},
    /* Testing having a key syntax in an array. */
    {"[\"x\":0]", NULL},
    /* Testing invalid numbers. */
    {"1.", NULL},
    {"1e", NULL},
    {".12", NULL},
    {"1.x", NULL},
    {"1.12x", NULL},
    {"1ex", NULL},
    {"1e12x", NULL},
    {".12x", NULL},
    {"000", NULL},
};

static void test_pairs() {
  unsigned i;

  for (i = 0; i < GPR_ARRAY_SIZE(testing_pairs); i++) {
    testing_pair *pair = testing_pairs + i;
    char *scratchpad = gpr_strdup(pair->input);
    grpc_json *json;

    gpr_log(GPR_INFO, "parsing string %i - should %s", i,
            pair->output ? "succeed" : "fail");
    json = grpc_json_parse_string(scratchpad);

    if (pair->output) {
      char *output;

      GPR_ASSERT(json);
      output = grpc_json_dump_to_string(json, 0);
      GPR_ASSERT(output);
      gpr_log(GPR_INFO, "succeeded with output = %s", output);
      GPR_ASSERT(strcmp(output, pair->output) == 0);

      grpc_json_destroy(json);
      gpr_free(output);
    } else {
      gpr_log(GPR_INFO, "failed");
      GPR_ASSERT(!json);
    }

    gpr_free(scratchpad);
  }
}

static void test_atypical() {
  char *scratchpad = gpr_strdup("[[],[],[]]");
  grpc_json *json = grpc_json_parse_string(scratchpad);
  grpc_json *brother;

  GPR_ASSERT(json);
  GPR_ASSERT(json->child);
  brother = json->child->next;
  grpc_json_destroy(json->child);
  GPR_ASSERT(json->child == brother);
  grpc_json_destroy(json->child->next);
  grpc_json_destroy(json);
  gpr_free(scratchpad);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_pairs();
  test_atypical();
  gpr_log(GPR_INFO, "json_test success");
  return 0;
}
