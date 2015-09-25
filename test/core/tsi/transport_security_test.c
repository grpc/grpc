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

#include "src/core/tsi/transport_security.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include <openssl/crypto.h>

#include "src/core/support/string.h"
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

} cert_name_test_entry;

/* Largely inspired from:
   chromium/src/net/cert/x509_certificate_unittest.cc.
   TODO(jboeuf) uncomment test cases as we fix tsi_ssl_peer_matches_name. */
const cert_name_test_entry cert_name_test_entries[] = {
    {1, "foo.com", "foo.com", NULL},
    {1, "f", "f", NULL},
    {0, "h", "i", NULL},
    {1, "bar.foo.com", "*.foo.com", NULL},
    {1, "www.test.fr", "common.name",
     "*.test.com,*.test.co.uk,*.test.de,*.test.fr"},
    /*
       {1, "wwW.tESt.fr", "common.name", ",*.*,*.test.de,*.test.FR,www"},
     */
    {0, "f.uk", ".uk", NULL},
    {0, "w.bar.foo.com", "?.bar.foo.com", NULL},
    {0, "www.foo.com", "(www|ftp).foo.com", NULL},
    {0, "www.foo.com", "www.foo.com#", NULL}, /* # = null char. */
    {0, "www.foo.com", "", "www.foo.com#*.foo.com,#,#"},
    {0, "www.house.example", "ww.house.example", NULL},
    {0, "test.org", "", "www.test.org,*.test.org,*.org"},
    {0, "w.bar.foo.com", "w*.bar.foo.com", NULL},
    {0, "www.bar.foo.com", "ww*ww.bar.foo.com", NULL},
    {0, "wwww.bar.foo.com", "ww*ww.bar.foo.com", NULL},
    {0, "wwww.bar.foo.com", "w*w.bar.foo.com", NULL},
    {0, "wwww.bar.foo.com", "w*w.bar.foo.c0m", NULL},
    {0, "WALLY.bar.foo.com", "wa*.bar.foo.com", NULL},
    {0, "wally.bar.foo.com", "*Ly.bar.foo.com", NULL},
    /*
       {1, "ww%57.foo.com", "", "www.foo.com"},
       {1, "www&.foo.com", "www%26.foo.com", NULL},
     */

    /* Common name must not be used if subject alternative name was provided. */
    {0, "www.test.co.jp", "www.test.co.jp",
     "*.test.de,*.jp,www.test.co.uk,www.*.co.jp"},
    {0, "www.bar.foo.com", "www.bar.foo.com",
     "*.foo.com,*.*.foo.com,*.*.bar.foo.com,*..bar.foo.com,"},

    /* IDN tests */
    {1, "xn--poema-9qae5a.com.br", "xn--poema-9qae5a.com.br", NULL},
    {1, "www.xn--poema-9qae5a.com.br", "*.xn--poema-9qae5a.com.br", NULL},
    {0, "xn--poema-9qae5a.com.br", "",
     "*.xn--poema-9qae5a.com.br,"
     "xn--poema-*.com.br,"
     "xn--*-9qae5a.com.br,"
     "*--poema-9qae5a.com.br"},

    /* The following are adapted from the  examples quoted from
       http://tools.ietf.org/html/rfc6125#section-6.4.3
       (e.g., *.example.com would match foo.example.com but
       not bar.foo.example.com or example.com). */
    {1, "foo.example.com", "*.example.com", NULL},
    {0, "bar.foo.example.com", "*.example.com", NULL},
    {0, "example.com", "*.example.com", NULL},

    /* Partial wildcards are disallowed, though RFC 2818 rules allow them.
       That is, forms such as baz*.example.net, *baz.example.net, and
       b*z.example.net should NOT match domains. Instead, the wildcard must
       always be the left-most label, and only a single label. */
    {0, "baz1.example.net", "baz*.example.net", NULL},
    {0, "foobaz.example.net", "*baz.example.net", NULL},
    {0, "buzz.example.net", "b*z.example.net", NULL},
    {0, "www.test.example.net", "www.*.example.net", NULL},

    /* Wildcards should not be valid for public registry controlled domains,
       and unknown/unrecognized domains, at least three domain components must
       be present. */
    {1, "www.test.example", "*.test.example", NULL},
    {1, "test.example.co.uk", "*.example.co.uk", NULL},
    {0, "test.example", "*.example", NULL},
    /*
       {0, "example.co.uk", "*.co.uk", NULL},
     */
    {0, "foo.com", "*.com", NULL},
    {0, "foo.us", "*.us", NULL},
    {0, "foo", "*", NULL},

    /* IDN variants of wildcards and registry controlled domains. */
    {1, "www.xn--poema-9qae5a.com.br", "*.xn--poema-9qae5a.com.br", NULL},
    {1, "test.example.xn--mgbaam7a8h", "*.example.xn--mgbaam7a8h", NULL},
    /*
       {0, "xn--poema-9qae5a.com.br", "*.com.br", NULL},
     */
    {0, "example.xn--mgbaam7a8h", "*.xn--mgbaam7a8h", NULL},

    /* Wildcards should be permissible for 'private' registry controlled
       domains. */
    {1, "www.appspot.com", "*.appspot.com", NULL},
    {1, "foo.s3.amazonaws.com", "*.s3.amazonaws.com", NULL},

    /* Multiple wildcards are not valid. */
    {0, "foo.example.com", "*.*.com", NULL},
    {0, "foo.bar.example.com", "*.bar.*.com", NULL},

    /* Absolute vs relative DNS name tests. Although not explicitly specified
       in RFC 6125, absolute reference names (those ending in a .) should
       match either absolute or relative presented names. */
    {1, "foo.com", "foo.com.", NULL},
    {1, "foo.com.", "foo.com", NULL},
    {1, "foo.com.", "foo.com.", NULL},
    {1, "f", "f.", NULL},
    {1, "f.", "f", NULL},
    {1, "f.", "f.", NULL},
    {1, "www-3.bar.foo.com", "*.bar.foo.com.", NULL},
    {1, "www-3.bar.foo.com.", "*.bar.foo.com", NULL},
    {1, "www-3.bar.foo.com.", "*.bar.foo.com.", NULL},
    {0, ".", ".", NULL},
    {0, "example.com", "*.com.", NULL},
    {0, "example.com.", "*.com", NULL},
    {0, "example.com.", "*.com.", NULL},
    {0, "foo.", "*.", NULL},
    {0, "foo", "*.", NULL},
    /*
       {0, "foo.co.uk", "*.co.uk.", NULL},
       {0, "foo.co.uk.", "*.co.uk.", NULL},
     */

    /* An empty CN is OK. */
    {1, "test.foo.com", "", "test.foo.com"},

    /* An IP should not be used for the CN. */
    {0, "173.194.195.139", "173.194.195.139", NULL},
};

typedef struct name_list {
  const char *name;
  struct name_list *next;
} name_list;

typedef struct {
  size_t name_count;
  char *buffer;
  name_list *names;
} parsed_dns_names;

name_list *name_list_add(const char *n) {
  name_list *result = gpr_malloc(sizeof(name_list));
  result->name = n;
  result->next = NULL;
  return result;
}

static parsed_dns_names parse_dns_names(const char *dns_names_str) {
  parsed_dns_names result;
  name_list *current_nl;
  size_t i;
  memset(&result, 0, sizeof(parsed_dns_names));
  if (dns_names_str == 0) return result;
  result.name_count = 1;
  result.buffer = gpr_strdup(dns_names_str);
  result.names = name_list_add(result.buffer);
  current_nl = result.names;
  for (i = 0; i < strlen(dns_names_str); i++) {
    if (dns_names_str[i] == ',') {
      result.buffer[i] = '\0';
      result.name_count++;
      i++;
      current_nl->next = name_list_add(result.buffer + i);
      current_nl = current_nl->next;
    }
  }
  return result;
}

static void destruct_parsed_dns_names(parsed_dns_names *pdn) {
  name_list *nl = pdn->names;
  if (pdn->buffer != NULL) gpr_free(pdn->buffer);
  while (nl != NULL) {
    name_list *to_be_free = nl;
    nl = nl->next;
    gpr_free(to_be_free);
  }
}

static char *processed_dns_name(const char *dns_name) {
  char *result = gpr_strdup(dns_name);
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
  parsed_dns_names dns_entries = parse_dns_names(entry->dns_names);
  nl = dns_entries.names;
  GPR_ASSERT(tsi_construct_peer(1 + dns_entries.name_count, &peer) == TSI_OK);
  GPR_ASSERT(tsi_construct_string_peer_property_from_cstring(
                 TSI_X509_SUBJECT_COMMON_NAME_PEER_PROPERTY, entry->common_name,
                 &peer.properties[0]) == TSI_OK);
  i = 1;
  while (nl != NULL) {
    char *processed = processed_dns_name(nl->name);
    GPR_ASSERT(tsi_construct_string_peer_property(
                   TSI_X509_SUBJECT_ALTERNATIVE_NAME_PEER_PROPERTY, processed,
                   strlen(nl->name), &peer.properties[i++]) == TSI_OK);
    nl = nl->next;
    gpr_free(processed);
  }
  destruct_parsed_dns_names(&dns_entries);
  return peer;
}

char *cert_name_test_entry_to_string(const cert_name_test_entry *entry) {
  char *s;
  gpr_asprintf(
      &s, "{ success = %s, host_name = %s, common_name = %s, dns_names = %s}",
      entry->expected ? "true" : "false", entry->host_name, entry->common_name,
      entry->dns_names != NULL ? entry->dns_names : "");
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

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_peer_matches_name();
  return 0;
}
