/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/iomgr/error.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/avl.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#ifdef GPR_WINDOWS
#include <grpc/support/log_windows.h>
#endif

#include "src/core/lib/profiling/timers.h"

static void destroy_integer(void *key) {}

static void *copy_integer(void *key) { return key; }

static long compare_integers(void *key1, void *key2) {
  return GPR_ICMP((uintptr_t)key1, (uintptr_t)key2);
}

static void destroy_string(void *str) { gpr_free(str); }

static void *copy_string(void *str) { return gpr_strdup(str); }

static void destroy_err(void *err) { GRPC_ERROR_UNREF(err); }

static void *copy_err(void *err) { return GRPC_ERROR_REF(err); }

static void destroy_time(void *tm) { gpr_free(tm); }

static gpr_timespec *box_time(gpr_timespec tm) {
  gpr_timespec *out = gpr_malloc(sizeof(*out));
  *out = tm;
  return out;
}

static void *copy_time(void *tm) { return box_time(*(gpr_timespec *)tm); }

static const gpr_avl_vtable avl_vtable_ints = {destroy_integer, copy_integer,
                                               compare_integers,
                                               destroy_integer, copy_integer};

static const gpr_avl_vtable avl_vtable_strs = {destroy_integer, copy_integer,
                                               compare_integers, destroy_string,
                                               copy_string};

static const gpr_avl_vtable avl_vtable_times = {
    destroy_integer, copy_integer, compare_integers, destroy_time, copy_time};

static const gpr_avl_vtable avl_vtable_errs = {
    destroy_integer, copy_integer, compare_integers, destroy_err, copy_err};

static const char *error_int_name(grpc_error_ints key) {
  switch (key) {
    case GRPC_ERROR_INT_ERRNO:
      return "errno";
    case GRPC_ERROR_INT_FILE_LINE:
      return "file_line";
    case GRPC_ERROR_INT_STREAM_ID:
      return "stream_id";
    case GRPC_ERROR_INT_GRPC_STATUS:
      return "grpc_status";
    case GRPC_ERROR_INT_OFFSET:
      return "offset";
    case GRPC_ERROR_INT_INDEX:
      return "index";
    case GRPC_ERROR_INT_SIZE:
      return "size";
    case GRPC_ERROR_INT_HTTP2_ERROR:
      return "http2_error";
    case GRPC_ERROR_INT_TSI_CODE:
      return "tsi_code";
    case GRPC_ERROR_INT_SECURITY_STATUS:
      return "security_status";
    case GRPC_ERROR_INT_FD:
      return "fd";
    case GRPC_ERROR_INT_WSA_ERROR:
      return "wsa_error";
    case GRPC_ERROR_INT_HTTP_STATUS:
      return "http_status";
    case GRPC_ERROR_INT_LIMIT:
      return "limit";
    case GRPC_ERROR_INT_OCCURRED_DURING_WRITE:
      return "occurred_during_write";
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

static const char *error_str_name(grpc_error_strs key) {
  switch (key) {
    case GRPC_ERROR_STR_DESCRIPTION:
      return "description";
    case GRPC_ERROR_STR_OS_ERROR:
      return "os_error";
    case GRPC_ERROR_STR_TARGET_ADDRESS:
      return "target_address";
    case GRPC_ERROR_STR_SYSCALL:
      return "syscall";
    case GRPC_ERROR_STR_FILE:
      return "file";
    case GRPC_ERROR_STR_GRPC_MESSAGE:
      return "grpc_message";
    case GRPC_ERROR_STR_RAW_BYTES:
      return "raw_bytes";
    case GRPC_ERROR_STR_TSI_ERROR:
      return "tsi_error";
    case GRPC_ERROR_STR_FILENAME:
      return "filename";
    case GRPC_ERROR_STR_QUEUED_BUFFERS:
      return "queued_buffers";
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

static const char *error_time_name(grpc_error_times key) {
  switch (key) {
    case GRPC_ERROR_TIME_CREATED:
      return "created";
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

struct grpc_error {
  gpr_refcount refs;
  gpr_avl ints;
  gpr_avl strs;
  gpr_avl times;
  gpr_avl errs;
  uintptr_t next_err;
};

static bool is_special(grpc_error *err) {
  return err == GRPC_ERROR_NONE || err == GRPC_ERROR_OOM ||
         err == GRPC_ERROR_CANCELLED;
}

#ifdef GRPC_ERROR_REFCOUNT_DEBUG
grpc_error *grpc_error_ref(grpc_error *err, const char *file, int line,
                           const char *func) {
  if (is_special(err)) return err;
  gpr_log(GPR_DEBUG, "%p: %" PRIdPTR " -> %" PRIdPTR " [%s:%d %s]", err,
          err->refs.count, err->refs.count + 1, file, line, func);
  gpr_ref(&err->refs);
  return err;
}
#else
grpc_error *grpc_error_ref(grpc_error *err) {
  if (is_special(err)) return err;
  gpr_ref(&err->refs);
  return err;
}
#endif

static void error_destroy(grpc_error *err) {
  GPR_ASSERT(!is_special(err));
  gpr_avl_unref(err->ints);
  gpr_avl_unref(err->strs);
  gpr_avl_unref(err->errs);
  gpr_avl_unref(err->times);
  gpr_free(err);
}

#ifdef GRPC_ERROR_REFCOUNT_DEBUG
void grpc_error_unref(grpc_error *err, const char *file, int line,
                      const char *func) {
  if (is_special(err)) return;
  gpr_log(GPR_DEBUG, "%p: %" PRIdPTR " -> %" PRIdPTR " [%s:%d %s]", err,
          err->refs.count, err->refs.count - 1, file, line, func);
  if (gpr_unref(&err->refs)) {
    error_destroy(err);
  }
}
#else
void grpc_error_unref(grpc_error *err) {
  if (is_special(err)) return;
  if (gpr_unref(&err->refs)) {
    error_destroy(err);
  }
}
#endif

grpc_error *grpc_error_create(const char *file, int line, const char *desc,
                              grpc_error **referencing,
                              size_t num_referencing) {
  GPR_TIMER_BEGIN("grpc_error_create", 0);
  grpc_error *err = gpr_malloc(sizeof(*err));
  if (err == NULL) {  // TODO(ctiller): make gpr_malloc return NULL
    return GRPC_ERROR_OOM;
  }
#ifdef GRPC_ERROR_REFCOUNT_DEBUG
  gpr_log(GPR_DEBUG, "%p create [%s:%d]", err, file, line);
#endif
  err->ints = gpr_avl_add(gpr_avl_create(&avl_vtable_ints),
                          (void *)(uintptr_t)GRPC_ERROR_INT_FILE_LINE,
                          (void *)(uintptr_t)line);
  err->strs = gpr_avl_add(
      gpr_avl_add(gpr_avl_create(&avl_vtable_strs),
                  (void *)(uintptr_t)GRPC_ERROR_STR_FILE, gpr_strdup(file)),
      (void *)(uintptr_t)GRPC_ERROR_STR_DESCRIPTION, gpr_strdup(desc));
  err->errs = gpr_avl_create(&avl_vtable_errs);
  err->next_err = 0;
  for (size_t i = 0; i < num_referencing; i++) {
    if (referencing[i] == GRPC_ERROR_NONE) continue;
    err->errs = gpr_avl_add(err->errs, (void *)(err->next_err++),
                            GRPC_ERROR_REF(referencing[i]));
  }
  err->times = gpr_avl_add(gpr_avl_create(&avl_vtable_times),
                           (void *)(uintptr_t)GRPC_ERROR_TIME_CREATED,
                           box_time(gpr_now(GPR_CLOCK_REALTIME)));
  gpr_ref_init(&err->refs, 1);
  GPR_TIMER_END("grpc_error_create", 0);
  return err;
}

static grpc_error *copy_error_and_unref(grpc_error *in) {
  GPR_TIMER_BEGIN("copy_error_and_unref", 0);
  grpc_error *out;
  if (is_special(in)) {
    if (in == GRPC_ERROR_NONE)
      out = GRPC_ERROR_CREATE("no error");
    else if (in == GRPC_ERROR_OOM)
      out = GRPC_ERROR_CREATE("oom");
    else if (in == GRPC_ERROR_CANCELLED)
      out =
          grpc_error_set_int(GRPC_ERROR_CREATE("cancelled"),
                             GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_CANCELLED);
    else
      out = GRPC_ERROR_CREATE("unknown");
  } else {
    out = gpr_malloc(sizeof(*out));
#ifdef GRPC_ERROR_REFCOUNT_DEBUG
    gpr_log(GPR_DEBUG, "%p create copying %p", out, in);
#endif
    out->ints = gpr_avl_ref(in->ints);
    out->strs = gpr_avl_ref(in->strs);
    out->errs = gpr_avl_ref(in->errs);
    out->times = gpr_avl_ref(in->times);
    out->next_err = in->next_err;
    gpr_ref_init(&out->refs, 1);
    GRPC_ERROR_UNREF(in);
  }
  GPR_TIMER_END("copy_error_and_unref", 0);
  return out;
}

grpc_error *grpc_error_set_int(grpc_error *src, grpc_error_ints which,
                               intptr_t value) {
  GPR_TIMER_BEGIN("grpc_error_set_int", 0);
  grpc_error *new = copy_error_and_unref(src);
  new->ints = gpr_avl_add(new->ints, (void *)(uintptr_t)which, (void *)value);
  GPR_TIMER_END("grpc_error_set_int", 0);
  return new;
}

bool grpc_error_get_int(grpc_error *err, grpc_error_ints which, intptr_t *p) {
  GPR_TIMER_BEGIN("grpc_error_get_int", 0);
  void *pp;
  if (is_special(err)) {
    if (err == GRPC_ERROR_CANCELLED && which == GRPC_ERROR_INT_GRPC_STATUS) {
      *p = GRPC_STATUS_CANCELLED;
      GPR_TIMER_END("grpc_error_get_int", 0);
      return true;
    }
    GPR_TIMER_END("grpc_error_get_int", 0);
    return false;
  }
  if (gpr_avl_maybe_get(err->ints, (void *)(uintptr_t)which, &pp)) {
    if (p != NULL) *p = (intptr_t)pp;
    GPR_TIMER_END("grpc_error_get_int", 0);
    return true;
  }
  GPR_TIMER_END("grpc_error_get_int", 0);
  return false;
}

grpc_error *grpc_error_set_str(grpc_error *src, grpc_error_strs which,
                               const char *value) {
  GPR_TIMER_BEGIN("grpc_error_set_str", 0);
  grpc_error *new = copy_error_and_unref(src);
  new->strs =
      gpr_avl_add(new->strs, (void *)(uintptr_t)which, gpr_strdup(value));
  GPR_TIMER_END("grpc_error_set_str", 0);
  return new;
}

const char *grpc_error_get_str(grpc_error *err, grpc_error_strs which) {
  if (is_special(err)) return NULL;
  return gpr_avl_get(err->strs, (void *)(uintptr_t)which);
}

typedef struct {
  grpc_error *error;
  grpc_status_code code;
  const char *msg;
} special_error_status_map;
static special_error_status_map error_status_map[] = {
    {GRPC_ERROR_NONE, GRPC_STATUS_OK, ""},
    {GRPC_ERROR_CANCELLED, GRPC_STATUS_CANCELLED, "RPC cancelled"},
    {GRPC_ERROR_OOM, GRPC_STATUS_RESOURCE_EXHAUSTED, "Out of memory"},
};

static grpc_error *recursively_find_error_with_status(grpc_error *error,
                                                      intptr_t *status) {
  // If the error itself has a status code, return it.
  if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, status)) {
    return error;
  }
  // Otherwise, search through its children.
  intptr_t key = 0;
  while (true) {
    grpc_error *child_error = gpr_avl_get(error->errs, (void *)key++);
    if (child_error == NULL) break;
    grpc_error *result =
        recursively_find_error_with_status(child_error, status);
    if (result != NULL) return result;
  }
  return NULL;
}

void grpc_error_get_status(grpc_error *error, grpc_status_code *code,
                           const char **msg) {
  // Handle special errors via the static map.
  for (size_t i = 0; i < GPR_ARRAY_SIZE(error_status_map); ++i) {
    if (error == error_status_map[i].error) {
      *code = error_status_map[i].code;
      *msg = error_status_map[i].msg;
      return;
    }
  }
  // Populate code.
  // Start with the parent error and recurse through the tree of children
  // until we find the first one that has a status code.
  intptr_t status = GRPC_STATUS_UNKNOWN;  // Default in case we don't find one.
  grpc_error *found_error = recursively_find_error_with_status(error, &status);
  *code = (grpc_status_code)status;
  // Now populate msg.
  // If we found an error with a status code above, use that; otherwise,
  // fall back to using the parent error.
  if (found_error == NULL) found_error = error;
  // If the error has a status message, use it.  Otherwise, fall back to
  // the error description.
  *msg = grpc_error_get_str(found_error, GRPC_ERROR_STR_GRPC_MESSAGE);
  if (*msg == NULL) {
    *msg = grpc_error_get_str(found_error, GRPC_ERROR_STR_DESCRIPTION);
    if (*msg == NULL) *msg = "uknown error";  // Just in case.
  }
}

grpc_error *grpc_error_add_child(grpc_error *src, grpc_error *child) {
  GPR_TIMER_BEGIN("grpc_error_add_child", 0);
  grpc_error *new = copy_error_and_unref(src);
  new->errs = gpr_avl_add(new->errs, (void *)(new->next_err++), child);
  GPR_TIMER_END("grpc_error_add_child", 0);
  return new;
}

static const char *no_error_string = "\"No Error\"";
static const char *oom_error_string = "\"Out of memory\"";
static const char *cancelled_error_string = "\"Cancelled\"";

typedef struct {
  char *key;
  char *value;
} kv_pair;

typedef struct {
  kv_pair *kvs;
  size_t num_kvs;
  size_t cap_kvs;
} kv_pairs;

static void append_kv(kv_pairs *kvs, char *key, char *value) {
  if (kvs->num_kvs == kvs->cap_kvs) {
    kvs->cap_kvs = GPR_MAX(3 * kvs->cap_kvs / 2, 4);
    kvs->kvs = gpr_realloc(kvs->kvs, sizeof(*kvs->kvs) * kvs->cap_kvs);
  }
  kvs->kvs[kvs->num_kvs].key = key;
  kvs->kvs[kvs->num_kvs].value = value;
  kvs->num_kvs++;
}

static void collect_kvs(gpr_avl_node *node, char *key(void *k),
                        char *fmt(void *v), kv_pairs *kvs) {
  if (node == NULL) return;
  append_kv(kvs, key(node->key), fmt(node->value));
  collect_kvs(node->left, key, fmt, kvs);
  collect_kvs(node->right, key, fmt, kvs);
}

static char *key_int(void *p) {
  return gpr_strdup(error_int_name((grpc_error_ints)(uintptr_t)p));
}

static char *key_str(void *p) {
  return gpr_strdup(error_str_name((grpc_error_strs)(uintptr_t)p));
}

static char *key_time(void *p) {
  return gpr_strdup(error_time_name((grpc_error_times)(uintptr_t)p));
}

static char *fmt_int(void *p) {
  char *s;
  gpr_asprintf(&s, "%" PRIdPTR, (intptr_t)p);
  return s;
}

static void append_chr(char c, char **s, size_t *sz, size_t *cap) {
  if (*sz == *cap) {
    *cap = GPR_MAX(8, 3 * *cap / 2);
    *s = gpr_realloc(*s, *cap);
  }
  (*s)[(*sz)++] = c;
}

static void append_str(const char *str, char **s, size_t *sz, size_t *cap) {
  for (const char *c = str; *c; c++) {
    append_chr(*c, s, sz, cap);
  }
}

static void append_esc_str(const char *str, char **s, size_t *sz, size_t *cap) {
  static const char *hex = "0123456789abcdef";
  append_chr('"', s, sz, cap);
  for (const uint8_t *c = (const uint8_t *)str; *c; c++) {
    if (*c < 32 || *c >= 127) {
      append_chr('\\', s, sz, cap);
      switch (*c) {
        case '\b':
          append_chr('b', s, sz, cap);
          break;
        case '\f':
          append_chr('f', s, sz, cap);
          break;
        case '\n':
          append_chr('n', s, sz, cap);
          break;
        case '\r':
          append_chr('r', s, sz, cap);
          break;
        case '\t':
          append_chr('t', s, sz, cap);
          break;
        default:
          append_chr('u', s, sz, cap);
          append_chr('0', s, sz, cap);
          append_chr('0', s, sz, cap);
          append_chr(hex[*c >> 4], s, sz, cap);
          append_chr(hex[*c & 0x0f], s, sz, cap);
          break;
      }
    } else {
      append_chr((char)*c, s, sz, cap);
    }
  }
  append_chr('"', s, sz, cap);
}

static char *fmt_str(void *p) {
  char *s = NULL;
  size_t sz = 0;
  size_t cap = 0;
  append_esc_str(p, &s, &sz, &cap);
  append_chr(0, &s, &sz, &cap);
  return s;
}

static char *fmt_time(void *p) {
  gpr_timespec tm = *(gpr_timespec *)p;
  char *out;
  char *pfx = "!!";
  switch (tm.clock_type) {
    case GPR_CLOCK_MONOTONIC:
      pfx = "@monotonic:";
      break;
    case GPR_CLOCK_REALTIME:
      pfx = "@";
      break;
    case GPR_CLOCK_PRECISE:
      pfx = "@precise:";
      break;
    case GPR_TIMESPAN:
      pfx = "";
      break;
  }
  gpr_asprintf(&out, "\"%s%" PRId64 ".%09d\"", pfx, tm.tv_sec, tm.tv_nsec);
  return out;
}

static void add_errs(gpr_avl_node *n, char **s, size_t *sz, size_t *cap,
                     bool *first) {
  if (n == NULL) return;
  add_errs(n->left, s, sz, cap, first);
  if (!*first) append_chr(',', s, sz, cap);
  *first = false;
  const char *e = grpc_error_string(n->value);
  append_str(e, s, sz, cap);
  grpc_error_free_string(e);
  add_errs(n->right, s, sz, cap, first);
}

static char *errs_string(grpc_error *err) {
  char *s = NULL;
  size_t sz = 0;
  size_t cap = 0;
  bool first = true;
  append_chr('[', &s, &sz, &cap);
  add_errs(err->errs.root, &s, &sz, &cap, &first);
  append_chr(']', &s, &sz, &cap);
  append_chr(0, &s, &sz, &cap);
  return s;
}

static int cmp_kvs(const void *a, const void *b) {
  const kv_pair *ka = a;
  const kv_pair *kb = b;
  return strcmp(ka->key, kb->key);
}

static const char *finish_kvs(kv_pairs *kvs) {
  char *s = NULL;
  size_t sz = 0;
  size_t cap = 0;

  append_chr('{', &s, &sz, &cap);
  for (size_t i = 0; i < kvs->num_kvs; i++) {
    if (i != 0) append_chr(',', &s, &sz, &cap);
    append_esc_str(kvs->kvs[i].key, &s, &sz, &cap);
    gpr_free(kvs->kvs[i].key);
    append_chr(':', &s, &sz, &cap);
    append_str(kvs->kvs[i].value, &s, &sz, &cap);
    gpr_free(kvs->kvs[i].value);
  }
  append_chr('}', &s, &sz, &cap);
  append_chr(0, &s, &sz, &cap);

  gpr_free(kvs->kvs);
  return s;
}

void grpc_error_free_string(const char *str) {
  if (str == no_error_string) return;
  if (str == oom_error_string) return;
  if (str == cancelled_error_string) return;
  gpr_free((char *)str);
}

const char *grpc_error_string(grpc_error *err) {
  GPR_TIMER_BEGIN("grpc_error_string", 0);
  if (err == GRPC_ERROR_NONE) return no_error_string;
  if (err == GRPC_ERROR_OOM) return oom_error_string;
  if (err == GRPC_ERROR_CANCELLED) return cancelled_error_string;

  kv_pairs kvs;
  memset(&kvs, 0, sizeof(kvs));

  collect_kvs(err->ints.root, key_int, fmt_int, &kvs);
  collect_kvs(err->strs.root, key_str, fmt_str, &kvs);
  collect_kvs(err->times.root, key_time, fmt_time, &kvs);
  if (!gpr_avl_is_empty(err->errs)) {
    append_kv(&kvs, gpr_strdup("referenced_errors"), errs_string(err));
  }

  qsort(kvs.kvs, kvs.num_kvs, sizeof(kv_pair), cmp_kvs);

  const char *out = finish_kvs(&kvs);
  GPR_TIMER_END("grpc_error_string", 0);
  return out;
}

grpc_error *grpc_os_error(const char *file, int line, int err,
                          const char *call_name) {
  return grpc_error_set_str(
      grpc_error_set_str(
          grpc_error_set_int(grpc_error_create(file, line, "OS Error", NULL, 0),
                             GRPC_ERROR_INT_ERRNO, err),
          GRPC_ERROR_STR_OS_ERROR, strerror(err)),
      GRPC_ERROR_STR_SYSCALL, call_name);
}

#ifdef GPR_WINDOWS
grpc_error *grpc_wsa_error(const char *file, int line, int err,
                           const char *call_name) {
  char *utf8_message = gpr_format_message(err);
  grpc_error *error = grpc_error_set_str(
      grpc_error_set_str(
          grpc_error_set_int(grpc_error_create(file, line, "OS Error", NULL, 0),
                             GRPC_ERROR_INT_WSA_ERROR, err),
          GRPC_ERROR_STR_OS_ERROR, utf8_message),
      GRPC_ERROR_STR_SYSCALL, call_name);
  gpr_free(utf8_message);
  return error;
}
#endif

bool grpc_log_if_error(const char *what, grpc_error *error, const char *file,
                       int line) {
  if (error == GRPC_ERROR_NONE) return true;
  const char *msg = grpc_error_string(error);
  gpr_log(file, line, GPR_LOG_SEVERITY_ERROR, "%s: %s", what, msg);
  grpc_error_free_string(msg);
  GRPC_ERROR_UNREF(error);
  return false;
}
