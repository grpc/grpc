/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/iomgr/error.h"

#include <string.h>

#include <grpc/status.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#ifdef GPR_WINDOWS
#include <grpc/support/log_windows.h>
#endif

#include "src/core/lib/debug/trace.h"
#include "src/core/lib/iomgr/error_internal.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"

#ifndef NDEBUG
grpc_tracer_flag grpc_trace_error_refcount =
    GRPC_TRACER_INITIALIZER(false, "error_refcount");
#endif

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
    case GRPC_ERROR_INT_MAX:
      GPR_UNREACHABLE_CODE(return "unknown");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

static const char *error_str_name(grpc_error_strs key) {
  switch (key) {
    case GRPC_ERROR_STR_KEY:
      return "key";
    case GRPC_ERROR_STR_VALUE:
      return "value";
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
    case GRPC_ERROR_STR_MAX:
      GPR_UNREACHABLE_CODE(return "unknown");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

static const char *error_time_name(grpc_error_times key) {
  switch (key) {
    case GRPC_ERROR_TIME_CREATED:
      return "created";
    case GRPC_ERROR_TIME_MAX:
      GPR_UNREACHABLE_CODE(return "unknown");
  }
  GPR_UNREACHABLE_CODE(return "unknown");
}

bool grpc_error_is_special(grpc_error *err) {
  return err == GRPC_ERROR_NONE || err == GRPC_ERROR_OOM ||
         err == GRPC_ERROR_CANCELLED;
}

#ifndef NDEBUG
grpc_error *grpc_error_ref(grpc_error *err, const char *file, int line) {
  if (grpc_error_is_special(err)) return err;
  if (GRPC_TRACER_ON(grpc_trace_error_refcount)) {
    gpr_log(GPR_DEBUG, "%p: %" PRIdPTR " -> %" PRIdPTR " [%s:%d]", err,
            gpr_atm_no_barrier_load(&err->atomics.refs.count),
            gpr_atm_no_barrier_load(&err->atomics.refs.count) + 1, file, line);
  }
  gpr_ref(&err->atomics.refs);
  return err;
}
#else
grpc_error *grpc_error_ref(grpc_error *err) {
  if (grpc_error_is_special(err)) return err;
  gpr_ref(&err->atomics.refs);
  return err;
}
#endif

static void unref_errs(grpc_error *err) {
  uint8_t slot = err->first_err;
  while (slot != UINT8_MAX) {
    grpc_linked_error *lerr = (grpc_linked_error *)(err->arena + slot);
    GRPC_ERROR_UNREF(lerr->err);
    GPR_ASSERT(err->last_err == slot ? lerr->next == UINT8_MAX
                                     : lerr->next != UINT8_MAX);
    slot = lerr->next;
  }
}

static void unref_slice(grpc_slice slice) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice_unref_internal(&exec_ctx, slice);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void unref_strs(grpc_error *err) {
  for (size_t which = 0; which < GRPC_ERROR_STR_MAX; ++which) {
    uint8_t slot = err->strs[which];
    if (slot != UINT8_MAX) {
      unref_slice(*(grpc_slice *)(err->arena + slot));
    }
  }
}

static void error_destroy(grpc_error *err) {
  GPR_ASSERT(!grpc_error_is_special(err));
  unref_errs(err);
  unref_strs(err);
  gpr_free((void *)gpr_atm_acq_load(&err->atomics.error_string));
  gpr_free(err);
}

#ifndef NDEBUG
void grpc_error_unref(grpc_error *err, const char *file, int line) {
  if (grpc_error_is_special(err)) return;
  if (GRPC_TRACER_ON(grpc_trace_error_refcount)) {
    gpr_log(GPR_DEBUG, "%p: %" PRIdPTR " -> %" PRIdPTR " [%s:%d]", err,
            gpr_atm_no_barrier_load(&err->atomics.refs.count),
            gpr_atm_no_barrier_load(&err->atomics.refs.count) - 1, file, line);
  }
  if (gpr_unref(&err->atomics.refs)) {
    error_destroy(err);
  }
}
#else
void grpc_error_unref(grpc_error *err) {
  if (grpc_error_is_special(err)) return;
  if (gpr_unref(&err->atomics.refs)) {
    error_destroy(err);
  }
}
#endif

static uint8_t get_placement(grpc_error **err, size_t size) {
  GPR_ASSERT(*err);
  uint8_t slots = (uint8_t)(size / sizeof(intptr_t));
  if ((*err)->arena_size + slots > (*err)->arena_capacity) {
    (*err)->arena_capacity =
        (uint8_t)GPR_MIN(UINT8_MAX - 1, (3 * (*err)->arena_capacity / 2));
    if ((*err)->arena_size + slots > (*err)->arena_capacity) {
      return UINT8_MAX;
    }
#ifndef NDEBUG
    grpc_error *orig = *err;
#endif
    *err = gpr_realloc(
        *err, sizeof(grpc_error) + (*err)->arena_capacity * sizeof(intptr_t));
#ifndef NDEBUG
    if (GRPC_TRACER_ON(grpc_trace_error_refcount)) {
      if (*err != orig) {
        gpr_log(GPR_DEBUG, "realloc %p -> %p", orig, *err);
      }
    }
#endif
  }
  uint8_t placement = (*err)->arena_size;
  (*err)->arena_size = (uint8_t)((*err)->arena_size + slots);
  return placement;
}

static void internal_set_int(grpc_error **err, grpc_error_ints which,
                             intptr_t value) {
  uint8_t slot = (*err)->ints[which];
  if (slot == UINT8_MAX) {
    slot = get_placement(err, sizeof(value));
    if (slot == UINT8_MAX) {
      gpr_log(GPR_ERROR, "Error %p is full, dropping int {\"%s\":%" PRIiPTR "}",
              *err, error_int_name(which), value);
      return;
    }
  }
  (*err)->ints[which] = slot;
  (*err)->arena[slot] = value;
}

static void internal_set_str(grpc_error **err, grpc_error_strs which,
                             grpc_slice value) {
  uint8_t slot = (*err)->strs[which];
  if (slot == UINT8_MAX) {
    slot = get_placement(err, sizeof(value));
    if (slot == UINT8_MAX) {
      const char *str = grpc_slice_to_c_string(value);
      gpr_log(GPR_ERROR, "Error %p is full, dropping string {\"%s\":\"%s\"}",
              *err, error_str_name(which), str);
      gpr_free((void *)str);
      return;
    }
  } else {
    unref_slice(*(grpc_slice *)((*err)->arena + slot));
  }
  (*err)->strs[which] = slot;
  memcpy((*err)->arena + slot, &value, sizeof(value));
}

static char *fmt_time(gpr_timespec tm);
static void internal_set_time(grpc_error **err, grpc_error_times which,
                              gpr_timespec value) {
  uint8_t slot = (*err)->times[which];
  if (slot == UINT8_MAX) {
    slot = get_placement(err, sizeof(value));
    if (slot == UINT8_MAX) {
      const char *time_str = fmt_time(value);
      gpr_log(GPR_ERROR, "Error %p is full, dropping \"%s\":\"%s\"}", *err,
              error_time_name(which), time_str);
      gpr_free((void *)time_str);
      return;
    }
  }
  (*err)->times[which] = slot;
  memcpy((*err)->arena + slot, &value, sizeof(value));
}

static void internal_add_error(grpc_error **err, grpc_error *new) {
  grpc_linked_error new_last = {new, UINT8_MAX};
  uint8_t slot = get_placement(err, sizeof(grpc_linked_error));
  if (slot == UINT8_MAX) {
    gpr_log(GPR_ERROR, "Error %p is full, dropping error %p = %s", *err, new,
            grpc_error_string(new));
    GRPC_ERROR_UNREF(new);
    return;
  }
  if ((*err)->first_err == UINT8_MAX) {
    GPR_ASSERT((*err)->last_err == UINT8_MAX);
    (*err)->last_err = slot;
    (*err)->first_err = slot;
  } else {
    GPR_ASSERT((*err)->last_err != UINT8_MAX);
    grpc_linked_error *old_last =
        (grpc_linked_error *)((*err)->arena + (*err)->last_err);
    old_last->next = slot;
    (*err)->last_err = slot;
  }
  memcpy((*err)->arena + slot, &new_last, sizeof(grpc_linked_error));
}

#define SLOTS_PER_INT (sizeof(intptr_t) / sizeof(intptr_t))
#define SLOTS_PER_STR (sizeof(grpc_slice) / sizeof(intptr_t))
#define SLOTS_PER_TIME (sizeof(gpr_timespec) / sizeof(intptr_t))
#define SLOTS_PER_LINKED_ERROR (sizeof(grpc_linked_error) / sizeof(intptr_t))

// size of storing one int and two slices and a timespec. For line, desc, file,
// and time created
#define DEFAULT_ERROR_CAPACITY \
  (SLOTS_PER_INT + (SLOTS_PER_STR * 2) + SLOTS_PER_TIME)

// It is very common to include and extra int and string in an error
#define SURPLUS_CAPACITY (2 * SLOTS_PER_INT + SLOTS_PER_TIME)

grpc_error *grpc_error_create(const char *file, int line, grpc_slice desc,
                              grpc_error **referencing,
                              size_t num_referencing) {
  GPR_TIMER_BEGIN("grpc_error_create", 0);
  uint8_t initial_arena_capacity = (uint8_t)(
      DEFAULT_ERROR_CAPACITY +
      (uint8_t)(num_referencing * SLOTS_PER_LINKED_ERROR) + SURPLUS_CAPACITY);
  grpc_error *err =
      gpr_malloc(sizeof(*err) + initial_arena_capacity * sizeof(intptr_t));
  if (err == NULL) {  // TODO(ctiller): make gpr_malloc return NULL
    return GRPC_ERROR_OOM;
  }
#ifndef NDEBUG
  if (GRPC_TRACER_ON(grpc_trace_error_refcount)) {
    gpr_log(GPR_DEBUG, "%p create [%s:%d]", err, file, line);
  }
#endif

  err->arena_size = 0;
  err->arena_capacity = initial_arena_capacity;
  err->first_err = UINT8_MAX;
  err->last_err = UINT8_MAX;

  memset(err->ints, UINT8_MAX, GRPC_ERROR_INT_MAX);
  memset(err->strs, UINT8_MAX, GRPC_ERROR_STR_MAX);
  memset(err->times, UINT8_MAX, GRPC_ERROR_TIME_MAX);

  internal_set_int(&err, GRPC_ERROR_INT_FILE_LINE, line);
  internal_set_str(&err, GRPC_ERROR_STR_FILE,
                   grpc_slice_from_static_string(file));
  internal_set_str(&err, GRPC_ERROR_STR_DESCRIPTION, desc);

  for (size_t i = 0; i < num_referencing; ++i) {
    if (referencing[i] == GRPC_ERROR_NONE) continue;
    internal_add_error(
        &err,
        GRPC_ERROR_REF(
            referencing[i]));  // TODO(ncteisen), change ownership semantics
  }

  internal_set_time(&err, GRPC_ERROR_TIME_CREATED, gpr_now(GPR_CLOCK_REALTIME));

  gpr_atm_no_barrier_store(&err->atomics.error_string, 0);
  gpr_ref_init(&err->atomics.refs, 1);
  GPR_TIMER_END("grpc_error_create", 0);
  return err;
}

static void ref_strs(grpc_error *err) {
  for (size_t i = 0; i < GRPC_ERROR_STR_MAX; ++i) {
    uint8_t slot = err->strs[i];
    if (slot != UINT8_MAX) {
      grpc_slice_ref_internal(*(grpc_slice *)(err->arena + slot));
    }
  }
}

static void ref_errs(grpc_error *err) {
  uint8_t slot = err->first_err;
  while (slot != UINT8_MAX) {
    grpc_linked_error *lerr = (grpc_linked_error *)(err->arena + slot);
    GRPC_ERROR_REF(lerr->err);
    slot = lerr->next;
  }
}

static grpc_error *copy_error_and_unref(grpc_error *in) {
  GPR_TIMER_BEGIN("copy_error_and_unref", 0);
  grpc_error *out;
  if (grpc_error_is_special(in)) {
    out = GRPC_ERROR_CREATE_FROM_STATIC_STRING("unknown");
    if (in == GRPC_ERROR_NONE) {
      internal_set_str(&out, GRPC_ERROR_STR_DESCRIPTION,
                       grpc_slice_from_static_string("no error"));
      internal_set_int(&out, GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_OK);
    } else if (in == GRPC_ERROR_OOM) {
      internal_set_str(&out, GRPC_ERROR_STR_DESCRIPTION,
                       grpc_slice_from_static_string("oom"));
    } else if (in == GRPC_ERROR_CANCELLED) {
      internal_set_str(&out, GRPC_ERROR_STR_DESCRIPTION,
                       grpc_slice_from_static_string("cancelled"));
      internal_set_int(&out, GRPC_ERROR_INT_GRPC_STATUS, GRPC_STATUS_CANCELLED);
    }
  } else if (gpr_ref_is_unique(&in->atomics.refs)) {
    out = in;
  } else {
    uint8_t new_arena_capacity = in->arena_capacity;
    // the returned err will be added to, so we ensure this is room to avoid
    // unneeded allocations.
    if (in->arena_capacity - in->arena_size < (uint8_t)SLOTS_PER_STR) {
      new_arena_capacity = (uint8_t)(3 * new_arena_capacity / 2);
    }
    out = gpr_malloc(sizeof(*in) + new_arena_capacity * sizeof(intptr_t));
#ifndef NDEBUG
    if (GRPC_TRACER_ON(grpc_trace_error_refcount)) {
      gpr_log(GPR_DEBUG, "%p create copying %p", out, in);
    }
#endif
    // bulk memcpy of the rest of the struct.
    size_t skip = sizeof(&out->atomics);
    memcpy((void *)((uintptr_t)out + skip), (void *)((uintptr_t)in + skip),
           sizeof(*in) + (in->arena_size * sizeof(intptr_t)) - skip);
    // manually set the atomics and the new capacity
    gpr_atm_no_barrier_store(&out->atomics.error_string, 0);
    gpr_ref_init(&out->atomics.refs, 1);
    out->arena_capacity = new_arena_capacity;
    ref_strs(out);
    ref_errs(out);
    GRPC_ERROR_UNREF(in);
  }
  GPR_TIMER_END("copy_error_and_unref", 0);
  return out;
}

grpc_error *grpc_error_set_int(grpc_error *src, grpc_error_ints which,
                               intptr_t value) {
  GPR_TIMER_BEGIN("grpc_error_set_int", 0);
  grpc_error *new = copy_error_and_unref(src);
  internal_set_int(&new, which, value);
  GPR_TIMER_END("grpc_error_set_int", 0);
  return new;
}

typedef struct {
  grpc_error *error;
  grpc_status_code code;
  const char *msg;
} special_error_status_map;
static special_error_status_map error_status_map[] = {
    {GRPC_ERROR_NONE, GRPC_STATUS_OK, ""},
    {GRPC_ERROR_CANCELLED, GRPC_STATUS_CANCELLED, "Cancelled"},
    {GRPC_ERROR_OOM, GRPC_STATUS_RESOURCE_EXHAUSTED, "Out of memory"},
};

bool grpc_error_get_int(grpc_error *err, grpc_error_ints which, intptr_t *p) {
  GPR_TIMER_BEGIN("grpc_error_get_int", 0);
  if (grpc_error_is_special(err)) {
    if (which == GRPC_ERROR_INT_GRPC_STATUS) {
      for (size_t i = 0; i < GPR_ARRAY_SIZE(error_status_map); i++) {
        if (error_status_map[i].error == err) {
          if (p != NULL) *p = error_status_map[i].code;
          GPR_TIMER_END("grpc_error_get_int", 0);
          return true;
        }
      }
    }
    GPR_TIMER_END("grpc_error_get_int", 0);
    return false;
  }
  uint8_t slot = err->ints[which];
  if (slot != UINT8_MAX) {
    if (p != NULL) *p = err->arena[slot];
    GPR_TIMER_END("grpc_error_get_int", 0);
    return true;
  }
  GPR_TIMER_END("grpc_error_get_int", 0);
  return false;
}

grpc_error *grpc_error_set_str(grpc_error *src, grpc_error_strs which,
                               grpc_slice str) {
  GPR_TIMER_BEGIN("grpc_error_set_str", 0);
  grpc_error *new = copy_error_and_unref(src);
  internal_set_str(&new, which, str);
  GPR_TIMER_END("grpc_error_set_str", 0);
  return new;
}

bool grpc_error_get_str(grpc_error *err, grpc_error_strs which,
                        grpc_slice *str) {
  if (grpc_error_is_special(err)) {
    if (which == GRPC_ERROR_STR_GRPC_MESSAGE) {
      for (size_t i = 0; i < GPR_ARRAY_SIZE(error_status_map); i++) {
        if (error_status_map[i].error == err) {
          *str = grpc_slice_from_static_string(error_status_map[i].msg);
          return true;
        }
      }
    }
    return false;
  }
  uint8_t slot = err->strs[which];
  if (slot != UINT8_MAX) {
    *str = *(grpc_slice *)(err->arena + slot);
    return true;
  } else {
    return false;
  }
}

grpc_error *grpc_error_add_child(grpc_error *src, grpc_error *child) {
  GPR_TIMER_BEGIN("grpc_error_add_child", 0);
  grpc_error *new = copy_error_and_unref(src);
  internal_add_error(&new, child);
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

static void append_esc_str(const uint8_t *str, size_t len, char **s, size_t *sz,
                           size_t *cap) {
  static const char *hex = "0123456789abcdef";
  append_chr('"', s, sz, cap);
  for (size_t i = 0; i < len; i++, str++) {
    if (*str < 32 || *str >= 127) {
      append_chr('\\', s, sz, cap);
      switch (*str) {
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
          append_chr(hex[*str >> 4], s, sz, cap);
          append_chr(hex[*str & 0x0f], s, sz, cap);
          break;
      }
    } else {
      append_chr((char)*str, s, sz, cap);
    }
  }
  append_chr('"', s, sz, cap);
}

static void append_kv(kv_pairs *kvs, char *key, char *value) {
  if (kvs->num_kvs == kvs->cap_kvs) {
    kvs->cap_kvs = GPR_MAX(3 * kvs->cap_kvs / 2, 4);
    kvs->kvs = gpr_realloc(kvs->kvs, sizeof(*kvs->kvs) * kvs->cap_kvs);
  }
  kvs->kvs[kvs->num_kvs].key = key;
  kvs->kvs[kvs->num_kvs].value = value;
  kvs->num_kvs++;
}

static char *key_int(grpc_error_ints which) {
  return gpr_strdup(error_int_name(which));
}

static char *fmt_int(intptr_t p) {
  char *s;
  gpr_asprintf(&s, "%" PRIdPTR, p);
  return s;
}

static void collect_ints_kvs(grpc_error *err, kv_pairs *kvs) {
  for (size_t which = 0; which < GRPC_ERROR_INT_MAX; ++which) {
    uint8_t slot = err->ints[which];
    if (slot != UINT8_MAX) {
      append_kv(kvs, key_int((grpc_error_ints)which),
                fmt_int(err->arena[slot]));
    }
  }
}

static char *key_str(grpc_error_strs which) {
  return gpr_strdup(error_str_name(which));
}

static char *fmt_str(grpc_slice slice) {
  char *s = NULL;
  size_t sz = 0;
  size_t cap = 0;
  append_esc_str((const uint8_t *)GRPC_SLICE_START_PTR(slice),
                 GRPC_SLICE_LENGTH(slice), &s, &sz, &cap);
  append_chr(0, &s, &sz, &cap);
  return s;
}

static void collect_strs_kvs(grpc_error *err, kv_pairs *kvs) {
  for (size_t which = 0; which < GRPC_ERROR_STR_MAX; ++which) {
    uint8_t slot = err->strs[which];
    if (slot != UINT8_MAX) {
      append_kv(kvs, key_str((grpc_error_strs)which),
                fmt_str(*(grpc_slice *)(err->arena + slot)));
    }
  }
}

static char *key_time(grpc_error_times which) {
  return gpr_strdup(error_time_name(which));
}

static char *fmt_time(gpr_timespec tm) {
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

static void collect_times_kvs(grpc_error *err, kv_pairs *kvs) {
  for (size_t which = 0; which < GRPC_ERROR_TIME_MAX; ++which) {
    uint8_t slot = err->times[which];
    if (slot != UINT8_MAX) {
      append_kv(kvs, key_time((grpc_error_times)which),
                fmt_time(*(gpr_timespec *)(err->arena + slot)));
    }
  }
}

static void add_errs(grpc_error *err, char **s, size_t *sz, size_t *cap) {
  uint8_t slot = err->first_err;
  bool first = true;
  while (slot != UINT8_MAX) {
    grpc_linked_error *lerr = (grpc_linked_error *)(err->arena + slot);
    if (!first) append_chr(',', s, sz, cap);
    first = false;
    const char *e = grpc_error_string(lerr->err);
    append_str(e, s, sz, cap);
    GPR_ASSERT(err->last_err == slot ? lerr->next == UINT8_MAX
                                     : lerr->next != UINT8_MAX);
    slot = lerr->next;
  }
}

static char *errs_string(grpc_error *err) {
  char *s = NULL;
  size_t sz = 0;
  size_t cap = 0;
  append_chr('[', &s, &sz, &cap);
  add_errs(err, &s, &sz, &cap);
  append_chr(']', &s, &sz, &cap);
  append_chr(0, &s, &sz, &cap);
  return s;
}

static int cmp_kvs(const void *a, const void *b) {
  const kv_pair *ka = a;
  const kv_pair *kb = b;
  return strcmp(ka->key, kb->key);
}

static char *finish_kvs(kv_pairs *kvs) {
  char *s = NULL;
  size_t sz = 0;
  size_t cap = 0;

  append_chr('{', &s, &sz, &cap);
  for (size_t i = 0; i < kvs->num_kvs; i++) {
    if (i != 0) append_chr(',', &s, &sz, &cap);
    append_esc_str((const uint8_t *)kvs->kvs[i].key, strlen(kvs->kvs[i].key),
                   &s, &sz, &cap);
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

const char *grpc_error_string(grpc_error *err) {
  GPR_TIMER_BEGIN("grpc_error_string", 0);
  if (err == GRPC_ERROR_NONE) return no_error_string;
  if (err == GRPC_ERROR_OOM) return oom_error_string;
  if (err == GRPC_ERROR_CANCELLED) return cancelled_error_string;

  void *p = (void *)gpr_atm_acq_load(&err->atomics.error_string);
  if (p != NULL) {
    GPR_TIMER_END("grpc_error_string", 0);
    return p;
  }

  kv_pairs kvs;
  memset(&kvs, 0, sizeof(kvs));

  collect_ints_kvs(err, &kvs);
  collect_strs_kvs(err, &kvs);
  collect_times_kvs(err, &kvs);
  if (err->first_err != UINT8_MAX) {
    append_kv(&kvs, gpr_strdup("referenced_errors"), errs_string(err));
  }

  qsort(kvs.kvs, kvs.num_kvs, sizeof(kv_pair), cmp_kvs);

  char *out = finish_kvs(&kvs);

  if (!gpr_atm_rel_cas(&err->atomics.error_string, 0, (gpr_atm)out)) {
    gpr_free(out);
    out = (char *)gpr_atm_no_barrier_load(&err->atomics.error_string);
  }

  GPR_TIMER_END("grpc_error_string", 0);
  return out;
}

grpc_error *grpc_os_error(const char *file, int line, int err,
                          const char *call_name) {
  return grpc_error_set_str(
      grpc_error_set_str(
          grpc_error_set_int(
              grpc_error_create(file, line,
                                grpc_slice_from_static_string("OS Error"), NULL,
                                0),
              GRPC_ERROR_INT_ERRNO, err),
          GRPC_ERROR_STR_OS_ERROR,
          grpc_slice_from_static_string(strerror(err))),
      GRPC_ERROR_STR_SYSCALL, grpc_slice_from_copied_string(call_name));
}

#ifdef GPR_WINDOWS
grpc_error *grpc_wsa_error(const char *file, int line, int err,
                           const char *call_name) {
  char *utf8_message = gpr_format_message(err);
  grpc_error *error = grpc_error_set_str(
      grpc_error_set_str(
          grpc_error_set_int(
              grpc_error_create(file, line,
                                grpc_slice_from_static_string("OS Error"), NULL,
                                0),
              GRPC_ERROR_INT_WSA_ERROR, err),
          GRPC_ERROR_STR_OS_ERROR, grpc_slice_from_copied_string(utf8_message)),
      GRPC_ERROR_STR_SYSCALL, grpc_slice_from_static_string(call_name));
  gpr_free(utf8_message);
  return error;
}
#endif

bool grpc_log_if_error(const char *what, grpc_error *error, const char *file,
                       int line) {
  if (error == GRPC_ERROR_NONE) return true;
  const char *msg = grpc_error_string(error);
  gpr_log(file, line, GPR_LOG_SEVERITY_ERROR, "%s: %s", what, msg);
  GRPC_ERROR_UNREF(error);
  return false;
}
