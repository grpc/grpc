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

#include "src/core/tsi/transport_security.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include <openssl/crypto.h>

#include "src/core/lib/support/string.h"
#include "src/core/tsi/fake_transport_security.h"
#include "src/core/tsi/ssl_transport_security.h"
#include "test/core/util/test_config.h"

typedef struct {
  /* 1 if success, 0 if failure. */
  int expected;

  /* Host name to match. */
  const char *host_name;

  /* Common name (CN). */
  const char *common_name;

  /* Comma separated list of certificate names to match against. Any occurrence
     of '#' will be replaced with a null character before processing. */
  const char *dns_names;

  /* Comma separated list of IP SANs to match aggainst */
  const char *ip_names;
} cert_name_test_entry;

/* Largely inspired from:
   chromium/src/net/cert/x509_certificate_unittest.cc.
   TODO(jboeuf) uncomment test cases as we fix tsi_ssl_peer_matches_name. */
const cert_name_test_entry cert_name_test_entries[] = {
    {1, "foo.com", "foo.com", NULL, NULL},
    {1, "f", "f", NULL, NULL},
    {0, "h", "i", NULL, NULL},
    {1, "bar.foo.com", "*.foo.com", NULL, NULL},
    {1, "www.test.fr", "common.name",
     "*.test.com,*.test.co.uk,*.test.de,*.test.fr", NULL},
    /*
       {1, "wwW.tESt.fr", "common.name", ",*.*,*.test.de,*.test.FR,www"},
     */
    {0, "f.uk", ".uk", NULL, NULL},
    {0, "w.bar.foo.com", "?.bar.foo.com", NULL, NULL},
    {0, "www.foo.com", "(www|ftp).foo.com", NULL, NULL},
    {0, "www.foo.com", "www.foo.com#", NULL, NULL}, /* # = null char. */
    {0, "www.foo.com", "", "www.foo.com#*.foo.com,#,#", NULL},
    {0, "www.house.example", "ww.house.example", NULL, NULL},
    {0, "test.org", "", "www.test.org,*.test.org,*.org", NULL},
    {0, "w.bar.foo.com", "w*.bar.foo.com", NULL, NULL},
    {0, "www.bar.foo.com", "ww*ww.bar.foo.com", NULL, NULL},
    {0, "wwww.bar.foo.com", "ww*ww.bar.foo.com", NULL, NULL},
    {0, "wwww.bar.foo.com", "w*w.bar.foo.com", NULL, NULL},
    {0, "wwww.bar.foo.com", "w*w.bar.foo.c0m", NULL, NULL},
    {0, "WALLY.bar.foo.com", "wa*.bar.foo.com", NULL, NULL},
    {0, "wally.bar.foo.com", "*Ly.bar.foo.com", NULL, NULL},
    /*
       {1, "ww%57.foo.com", "", "www.foo.com"},
       {1, "www&.foo.com", "www%26.foo.com", NULL},
     */

    /* Common name must not be used if subject alternative name was provided. */
    {0, "www.test.co.jp", "www.test.co.jp",
     "*.test.de,*.jp,www.test.co.uk,www.*.co.jp", NULL},
    {0, "www.bar.foo.com", "www.bar.foo.com",
     "*.foo.com,*.*.foo.com,*.*.bar.foo.com,*..bar.foo.com,", NULL},

    /* IDN tests */
    {1, "xn--poema-9qae5a.com.br", "xn--poema-9qae5a.com.br", NULL, NULL},
    {1, "www.xn--poema-9qae5a.com.br", "*.xn--poema-9qae5a.com.br", NULL, NULL},
    {0, "xn--poema-9qae5a.com.br", "",
     "*.xn--poema-9qae5a.com.br,"
     "xn--poema-*.com.br,"
     "xn--*-9qae5a.com.br,"
     "*--poema-9qae5a.com.br",
     NULL},

    /* The following are adapted from the  examples quoted from
       http://tools.ietf.org/html/rfc6125#section-6.4.3
       (e.g., *.example.com would match foo.example.com but
       not bar.foo.example.com or example.com). */
    {1, "foo.example.com", "*.example.com", NULL, NULL},
    {0, "bar.foo.example.com", "*.example.com", NULL, NULL},
    {0, "example.com", "*.example.com", NULL, NULL},

    /* Partial wildcards are disallowed, though RFC 2818 rules allow them.
       That is, forms such as baz*.example.net, *baz.example.net, and
       b*z.example.net should NOT match domains. Instead, the wildcard must
       always be the left-most label, and only a single label. */
    {0, "baz1.example.net", "baz*.example.net", NULL, NULL},
    {0, "foobaz.example.net", "*baz.example.net", NULL, NULL},
    {0, "buzz.example.net", "b*z.example.net", NULL, NULL},
    {0, "www.test.example.net", "www.*.example.net", NULL, NULL},

    /* Wildcards should not be valid for public registry controlled domains,
       and unknown/unrecognized domains, at least three domain components must
       be present. */
    {1, "www.test.example", "*.test.example", NULL, NULL},
    {1, "test.example.co.uk", "*.example.co.uk", NULL, NULL},
    {0, "test.example", "*.example", NULL, NULL},
    /*
       {0, "example.co.uk", "*.co.uk", NULL},
     */
    {0, "foo.com", "*.com", NULL, NULL},
    {0, "foo.us", "*.us", NULL, NULL},
    {0, "foo", "*", NULL, NULL},

    /* IDN variants of wildcards and registry controlled domains. */
    {1, "www.xn--poema-9qae5a.com.br", "*.xn--poema-9qae5a.com.br", NULL, NULL},
    {1, "test.example.xn--mgbaam7a8h", "*.example.xn--mgbaam7a8h", NULL, NULL},
    /*
       {0, "xn--poema-9qae5a.com.br", "*.com.br", NULL},
     */
    {0, "example.xn--mgbaam7a8h", "*.xn--mgbaam7a8h", NULL, NULL},

    /* Wildcards should be permissible for 'private' registry controlled
       domains. */
    {1, "www.appspot.com", "*.appspot.com", NULL, NULL},
    {1, "foo.s3.amazonaws.com", "*.s3.amazonaws.com", NULL, NULL},

    /* Multiple wildcards are not valid. */
    {0, "foo.example.com", "*.*.com", NULL, NULL},
    {0, "foo.bar.example.com", "*.bar.*.com", NULL, NULL},

    /* Absolute vs relative DNS name tests. Although not explicitly specified
       in RFC 6125, absolute reference names (those ending in a .) should
       match either absolute or relative presented names. */
    {1, "foo.com", "foo.com.", NULL, NULL},
    {1, "foo.com.", "foo.com", NULL, NULL},
    {1, "foo.com.", "foo.com.", NULL, NULL},
    {1, "f", "f.", NULL, NULL},
    {1, "f.", "f", NULL, NULL},
    {1, "f.", "f.", NULL, NULL},
    {1, "www-3.bar.foo.com", "*.bar.foo.com.", NULL, NULL},
    {1, "www-3.bar.foo.com.", "*.bar.foo.com", NULL, NULL},
    {1, "www-3.bar.foo.com.", "*.bar.foo.com.", NULL, NULL},
    {0, ".", ".", NULL, NULL},
    {0, "example.com", "*.com.", NULL, NULL},
    {0, "example.com.", "*.com", NULL, NULL},
    {0, "example.com.", "*.com.", NULL, NULL},
    {0, "foo.", "*.", NULL, NULL},
    {0, "foo", "*.", NULL, NULL},
    /*
       {0, "foo.co.uk", "*.co.uk.", NULL},
       {0, "foo.co.uk.", "*.co.uk.", NULL},
     */

    /* An empty CN is OK. */
    {1, "test.foo.com", "", "test.foo.com", NULL},

    /* An IP should not be used for the CN. */
    {0, "173.194.195.139", "173.194.195.139", NULL, NULL},
    /* An IP can be used if the SAN IP is present */
    {1, "173.194.195.139", "foo.example.com", NULL, "173.194.195.139"},
    {0, "173.194.195.139", "foo.example.com", NULL, "8.8.8.8"},
    {0, "173.194.195.139", "foo.example.com", NULL, "8.8.8.8,8.8.4.4"},
    {1, "173.194.195.139", "foo.example.com", NULL, "8.8.8.8,173.194.195.139"},
    {0, "173.194.195.139", "foo.example.com", NULL, "173.194.195.13"},
    {0, "2001:db8:a0b:12f0::1", "foo.example.com", NULL, "173.194.195.13"},
    {1, "2001:db8:a0b:12f0::1", "foo.example.com", NULL,
     "2001:db8:a0b:12f0::1"},
    {0, "2001:db8:a0b:12f0::1", "foo.example.com", NULL,
     "2001:db8:a0b:12f0::2"},
    {1, "2001:db8:a0b:12f0::1", "foo.example.com", NULL,
     "2001:db8:a0b:12f0::2,2001:db8:a0b:12f0::1,8.8.8.8"},
};

typedef struct name_list {
  const char *name;
  struct name_list *next;
} name_list;

typedef struct {
  size_t name_count;
  char *buffer;
  name_list *names;
} parsed_names;

name_list *name_list_add(const char *n) {
  name_list *result = gpr_malloc(sizeof(name_list));
  result->name = n;
  result->next = NULL;
  return result;
}

static parsed_names parse_names(const char *names_str) {
  parsed_names result;
  name_list *current_nl;
  size_t i;
  memset(&result, 0, sizeof(parsed_names));
  if (names_str == 0) return result;
  result.name_count = 1;
  result.buffer = gpr_strdup(names_str);
  result.names = name_list_add(result.buffer);
  current_nl = result.names;
  for (i = 0; i < strlen(names_str); i++) {
    if (names_str[i] == ',') {
      result.buffer[i] = '\0';
      result.name_count++;
      i++;
      current_nl->next = name_list_add(result.buffer + i);
      current_nl = current_nl->next;
    }
  }
  return result;
}

static void destruct_parsed_names(parsed_names *pdn) {
  name_list *nl = pdn->names;
  if (pdn->buffer != NULL) gpr_free(pdn->buffer);
  while (nl != NULL) {
    name_list *to_be_free = nl;
    nl = nl->next;
    gpr_free(to_be_free);
  }
}

static char *processed_name(const char *name) {
  char *result = gpr_strdup(name);
  size_t i;
  for (i = 0; i < strlen(result); i++) {
    if (result[i] == '#') {
      result[i] = '\0';
    }
  }
  return result;
}

static tsi_peer peer_from_cert_name_test_entry(
    const cert_name_test_entry *entry) {
  size_t i;
  tsi_peer peer;
  name_list *nl;
  parsed_names dns_entries = parse_names(entry->dns_names);
  parsed_names ip_entries = parse_names(entry->ip_names);
  nl = dns_entries.names;
  GPR_ASSERT(tsi_construct_peer(
                 1 + dns_entries.name_count + ip_entries.name_count, &peer) ==
             TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, entry->common_name,
                 &peer.properties[0]) == TSI_OK);
  i = 1;
  while (nl != NULL) {
    char *processed = processed_name(nl->name);
    GPR_ASSERT(tsi_construct_string_peer_property(
                   TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, processed,
                   strlen(nl->name), &peer.properties[i++]) == TSI_OK);
    nl = nl->next;
    gpr_free(processed);
  }

  nl = ip_entries.names;
  while (nl != NULL) {
    char *processed = processed_name(nl->name);
    GPR_ASSERT(tsi_construct_string_peer_property(
                   TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, processed,
                   strlen(nl->name), &peer.properties[i++]) == TSI_OK);
    nl = nl->next;
    gpr_free(processed);
  }
  destruct_parsed_names(&dns_entries);
  destruct_parsed_names(&ip_entries);
  return peer;
}

char *cert_name_test_entry_to_string(const cert_name_test_entry *entry) {
  char *s;
  gpr_asprintf(&s,
               "{ success = %s, host_name = %s, common_name = %s, dns_names = "
               "%s, ip_names = %s}",
               entry->expected ? "true" : "false", entry->host_name,
               entry->common_name,
               entry->dns_names != NULL ? entry->dns_names : "",
               entry->ip_names != NULL ? entry->ip_names : "");
  return s;
}

static void test_peer_matches_name(void) {
  size_t i = 0;
  for (i = 0; i < GPR_ARRAY_SIZE(cert_name_test_entries); i++) {
    const cert_name_test_entry *entry = &cert_name_test_entries[i];
    tsi_peer peer = peer_from_cert_name_test_entry(entry);
    int result = tsi_ssl_peer_matches_name(&peer, entry->host_name);
    if (result != entry->expected) {
      char *entry_str = cert_name_test_entry_to_string(entry);
      gpr_log(GPR_ERROR, "%s", entry_str);
      gpr_free(entry_str);
      GPR_ASSERT(0); /* Unexpected result. */
    }
    tsi_peer_destruct(&peer);
  }
}

typedef struct {
  tsi_result res;
  const char *str;
} tsi_result_string_pair;

static void test_result_strings(void) {
  const tsi_result_string_pair results[] = {
      {TSI_OK, "TSI_OK"},
      {TSI_UNKNOWN_ERROR, "TSI_UNKNOWN_ERROR"},
      {TSI_INVALID_ARGUMENT, "TSI_INVALID_ARGUMENT"},
      {TSI_PERMISSION_DENIED, "TSI_PERMISSION_DENIED"},
      {TSI_INCOMPLETE_DATA, "TSI_INCOMPLETE_DATA"},
      {TSI_FAILED_PRECONDITION, "TSI_FAILED_PRECONDITION"},
      {TSI_UNIMPLEMENTED, "TSI_UNIMPLEMENTED"},
      {TSI_INTERNAL_ERROR, "TSI_INTERNAL_ERROR"},
      {TSI_DATA_CORRUPTED, "TSI_DATA_CORRUPTED"},
      {TSI_NOT_FOUND, "TSI_NOT_FOUND"},
      {TSI_PROTOCOL_FAILURE, "TSI_PROTOCOL_FAILURE"},
      {TSI_HANDSHAKE_IN_PROGRESS, "TSI_HANDSHAKE_IN_PROGRESS"},
      {TSI_OUT_OF_RESOURCES, "TSI_OUT_OF_RESOURCES"}};
  size_t i;
  for (i = 0; i < GPR_ARRAY_SIZE(results); i++) {
    GPR_ASSERT(strcmp(results[i].str, tsi_result_to_string(results[i].res)) ==
               0);
  }
  GPR_ASSERT(strcmp("UNKNOWN", tsi_result_to_string((tsi_result)42)) == 0);
}

static void test_protector_invalid_args(void) {
  GPR_ASSERT(tsi_frame_protector_protect(NULL, NULL, NULL, NULL, NULL) ==
             TSI_INVALID_ARGUMENT);
  GPR_ASSERT(tsi_frame_protector_protect_flush(NULL, NULL, NULL, NULL) ==
             TSI_INVALID_ARGUMENT);
  GPR_ASSERT(tsi_frame_protector_unprotect(NULL, NULL, NULL, NULL, NULL) ==
             TSI_INVALID_ARGUMENT);
}

static void test_handshaker_invalid_args(void) {
  GPR_ASSERT(tsi_handshaker_get_result(NULL) == TSI_INVALID_ARGUMENT);
  GPR_ASSERT(tsi_handshaker_extract_peer(NULL, NULL) == TSI_INVALID_ARGUMENT);
  GPR_ASSERT(tsi_handshaker_create_frame_protector(NULL, NULL, NULL) ==
             TSI_INVALID_ARGUMENT);
  GPR_ASSERT(tsi_handshaker_process_bytes_from_peer(NULL, NULL, NULL) ==
             TSI_INVALID_ARGUMENT);
  GPR_ASSERT(tsi_handshaker_get_bytes_to_send_to_peer(NULL, NULL, NULL) ==
             TSI_INVALID_ARGUMENT);
  GPR_ASSERT(tsi_handshaker_next(NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL) ==
             TSI_INVALID_ARGUMENT);
}

static void test_handshaker_invalid_state(void) {
  tsi_handshaker *h = tsi_create_fake_handshaker(0);
  tsi_peer peer;
  tsi_frame_protector *p;
  GPR_ASSERT(tsi_handshaker_extract_peer(h, &peer) == TSI_FAILED_PRECONDITION);
  GPR_ASSERT(tsi_handshaker_create_frame_protector(h, NULL, &p) ==
             TSI_FAILED_PRECONDITION);
  tsi_handshaker_destroy(h);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_peer_matches_name();
  test_result_strings();
  test_protector_invalid_args();
  test_handshaker_invalid_args();
  test_handshaker_invalid_state();
  return 0;
}
