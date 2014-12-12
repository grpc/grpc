/*
 *
 * Copyright 2014, Google Inc.
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

#include "src/core/security/json_token.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/slice.h>
#include "test/core/util/test_config.h"

/* This JSON key was generated with the GCE console and revoked immediately.
   The identifiers have been changed as well.
   Maximum size for a string literal is 509 chars in C89, yay!  */
static const char test_json_key_str_part1[] =
    "{ \"private_key\": \"-----BEGIN PRIVATE KEY-----"
    "\nMIICeAIBADANBgkqhkiG9w0BAQEFAASCAmIwggJeAgEAAoGBAOEvJsnoHnyHkXcp\n7mJEqg"
    "WGjiw71NfXByguekSKho65FxaGbsnSM9SMQAqVk7Q2rG+I0OpsT0LrWQtZ\nyjSeg/"
    "rWBQvS4hle4LfijkP3J5BG+"
    "IXDMP8RfziNRQsenAXDNPkY4kJCvKux2xdD\nOnVF6N7dL3nTYZg+"
    "uQrNsMTz9UxVAgMBAAECgYEAzbLewe1xe9vy+2GoSsfib+28\nDZgSE6Bu/"
    "zuFoPrRc6qL9p2SsnV7txrunTyJkkOnPLND9ABAXybRTlcVKP/sGgza\n/"
    "8HpCqFYM9V8f34SBWfD4fRFT+n/"
    "73cfRUtGXdXpseva2lh8RilIQfPhNZAncenU\ngqXjDvpkypEusgXAykECQQD+";
static const char test_json_key_str_part2[] =
    "53XxNVnxBHsYb+AYEfklR96yVi8HywjVHP34+OQZ\nCslxoHQM8s+"
    "dBnjfScLu22JqkPv04xyxmt0QAKm9+vTdAkEA4ib7YvEAn2jXzcCI\nEkoy2L/"
    "XydR1GCHoacdfdAwiL2npOdnbvi4ZmdYRPY1LSTO058tQHKVXV7NLeCa3\nAARh2QJBAMKeDAG"
    "W303SQv2cZTdbeaLKJbB5drz3eo3j7dDKjrTD9JupixFbzcGw\n8FZi5c8idxiwC36kbAL6HzA"
    "ZoX+ofI0CQE6KCzPJTtYNqyShgKAZdJ8hwOcvCZtf\n6z8RJm0+"
    "6YBd38lfh5j8mZd7aHFf6I17j5AQY7oPEc47TjJj/"
    "5nZ68ECQQDvYuI3\nLyK5fS8g0SYbmPOL9TlcHDOqwG0mrX9qpg5DC2fniXNSrrZ64GTDKdzZY"
    "Ap6LI9W\nIqv4vr6y38N79TTC\n-----END PRIVATE KEY-----\n\", ";
static const char test_json_key_str_part3[] =
    "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
    "\"client_email\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
    "com\", \"client_id\": "
    "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
    "com\", \"type\": \"service_account\" }";

static char *test_json_key_str(const char *bad_part3) {
  const char *part3 = bad_part3 != NULL ? bad_part3 : test_json_key_str_part3;
  size_t result_len = strlen(test_json_key_str_part1) +
                      strlen(test_json_key_str_part2) + strlen(part3);
  char *result = gpr_malloc(result_len + 1);
  char *current = result;
  strcpy(result, test_json_key_str_part1);
  current += strlen(test_json_key_str_part1);
  strcpy(current, test_json_key_str_part2);
  current += strlen(test_json_key_str_part2);
  strcpy(current, part3);
  return result;
}

static void test_parse_json_key_success() {
  char *json_string = test_json_key_str(NULL);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(grpc_auth_json_key_is_valid(&json_key));
  GPR_ASSERT(json_key.type != NULL &&
             !(strcmp(json_key.type, "service_account")));
  GPR_ASSERT(json_key.private_key_id != NULL &&
             !strcmp(json_key.private_key_id,
                     "e6b5137873db8d2ef81e06a47289e6434ec8a165"));
  GPR_ASSERT(json_key.client_id != NULL &&
             !strcmp(json_key.client_id,
                     "777-abaslkan11hlb6nmim3bpspl31ud.apps."
                     "googleusercontent.com"));
  GPR_ASSERT(json_key.client_email != NULL &&
             !strcmp(json_key.client_email,
                     "777-abaslkan11hlb6nmim3bpspl31ud@developer."
                     "gserviceaccount.com"));
  GPR_ASSERT(json_key.private_key != NULL);
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_bad_json() {
  const char non_closing_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" ";
  char *json_string = test_json_key_str(non_closing_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_type() {
  const char no_type_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\" }";
  char *json_string = test_json_key_str(no_type_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_client_id() {
  const char no_client_id_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", "
      "\"type\": \"service_account\" }";
  char *json_string = test_json_key_str(no_client_id_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_client_email() {
  const char no_client_email_part3[] =
      "\"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" }";
  char *json_string = test_json_key_str(no_client_email_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_private_key_id() {
  const char no_private_key_id_part3[] =
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" }";
  char *json_string = test_json_key_str(no_private_key_id_part3);
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  gpr_free(json_string);
  grpc_auth_json_key_destruct(&json_key);
}

static void test_parse_json_key_failure_no_private_key() {
  const char no_private_key_json_string[] =
      "{ \"private_key_id\": \"e6b5137873db8d2ef81e06a47289e6434ec8a165\", "
      "\"client_email\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud@developer.gserviceaccount."
      "com\", \"client_id\": "
      "\"777-abaslkan11hlb6nmim3bpspl31ud.apps.googleusercontent."
      "com\", \"type\": \"service_account\" }";
  grpc_auth_json_key json_key =
      grpc_auth_json_key_create_from_string(no_private_key_json_string);
  GPR_ASSERT(!grpc_auth_json_key_is_valid(&json_key));
  grpc_auth_json_key_destruct(&json_key);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_parse_json_key_success();
  test_parse_json_key_failure_bad_json();
  test_parse_json_key_failure_no_type();
  test_parse_json_key_failure_no_client_id();
  test_parse_json_key_failure_no_client_email();
  test_parse_json_key_failure_no_private_key_id();
  test_parse_json_key_failure_no_private_key();
  return 0;
}
