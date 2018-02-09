/*
 *
 * Copyright 2015-2016 gRPC authors.
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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/json/json.h"

#include "test/core/util/test_config.h"

typedef struct testing_pair {
  const char* input;
  const char* output;
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
        " [ [ ] , { } , [ ] ] ",
        "[[],{},[]]",
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
    {"\\", nullptr},
    {"nu ll", nullptr},
    {"{\"foo\": bar}", nullptr},
    {"{\"foo\": bar\"x\"}", nullptr},
    {"fals", nullptr},
    {"0,0 ", nullptr},
    {"\"foo\",[]", nullptr},
    /* Testing unterminated string. */
    {"\"\\x", nullptr},
    /* Testing invalid UTF-16 number. */
    {"\"\\u123x", nullptr},
    {"{\"\\u123x", nullptr},
    /* Testing imbalanced surrogate pairs. */
    {"\"\\ud834f", nullptr},
    {"{\"\\ud834f\":0}", nullptr},
    {"\"\\ud834\\n", nullptr},
    {"{\"\\ud834\\n\":0}", nullptr},
    {"\"\\udd1ef", nullptr},
    {"{\"\\udd1ef\":0}", nullptr},
    {"\"\\ud834\\ud834\"", nullptr},
    {"{\"\\ud834\\ud834\"\":0}", nullptr},
    {"\"\\ud834\\u1234\"", nullptr},
    {"{\"\\ud834\\u1234\"\":0}", nullptr},
    {"\"\\ud834]\"", nullptr},
    {"{\"\\ud834]\"\":0}", nullptr},
    {"\"\\ud834 \"", nullptr},
    {"{\"\\ud834 \"\":0}", nullptr},
    {"\"\\ud834\\\\\"", nullptr},
    {"{\"\\ud834\\\\\"\":0}", nullptr},
    /* Testing embedded invalid whitechars. */
    {"\"\n\"", nullptr},
    {"\"\t\"", nullptr},
    /* Testing empty json data. */
    {"", nullptr},
    /* Testing extra characters after end of parsing. */
    {"{},", nullptr},
    /* Testing imbalanced containers. */
    {"{}}", nullptr},
    {"[]]", nullptr},
    {"{{}", nullptr},
    {"[[]", nullptr},
    {"[}", nullptr},
    {"{]", nullptr},
    /* Testing bad containers. */
    {"{x}", nullptr},
    {"{x=0,y}", nullptr},
    /* Testing trailing comma. */
    {"{,}", nullptr},
    {"[1,2,3,4,]", nullptr},
    {"{\"a\": 1, }", nullptr},
    /* Testing after-ending characters. */
    {"{}x", nullptr},
    /* Testing having a key syntax in an array. */
    {"[\"x\":0]", nullptr},
    /* Testing invalid numbers. */
    {"1.", nullptr},
    {"1e", nullptr},
    {".12", nullptr},
    {"1.x", nullptr},
    {"1.12x", nullptr},
    {"1ex", nullptr},
    {"1e12x", nullptr},
    {".12x", nullptr},
    {"000", nullptr},
};

static void test_pairs() {
  unsigned i;

  for (i = 0; i < GPR_ARRAY_SIZE(testing_pairs); i++) {
    testing_pair* pair = testing_pairs + i;
    char* scratchpad = gpr_strdup(pair->input);
    grpc_json* json;

    gpr_log(GPR_INFO, "parsing string %i - should %s", i,
            pair->output ? "succeed" : "fail");
    json = grpc_json_parse_string(scratchpad);

    if (pair->output) {
      char* output;

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
  char* scratchpad = gpr_strdup("[[],[],[]]");
  grpc_json* json = grpc_json_parse_string(scratchpad);
  grpc_json* brother;

  GPR_ASSERT(json);
  GPR_ASSERT(json->child);
  brother = json->child->next;
  grpc_json_destroy(json->child);
  GPR_ASSERT(json->child == brother);
  grpc_json_destroy(json->child->next);
  grpc_json_destroy(json);
  gpr_free(scratchpad);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  test_pairs();
  test_atypical();
  gpr_log(GPR_INFO, "json_test success");
  return 0;
}
