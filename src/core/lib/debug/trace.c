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

#include "src/core/lib/debug/trace.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "src/core/lib/support/env.h"

int grpc_tracer_set_enabled(const char *name, int enabled);

typedef struct tracer {
  grpc_tracer_flag *flag;
  struct tracer *next;
} tracer;
static tracer *tracers;

#ifdef GRPC_THREADSAFE_TRACER
#define TRACER_SET(flag, on) gpr_atm_no_barrier_store(&(flag).value, (on))
#else
#define TRACER_SET(flag, on) (flag).value = (on)
#endif

void grpc_register_tracer(grpc_tracer_flag *flag) {
  tracer *t = (tracer *)gpr_malloc(sizeof(*t));
  t->flag = flag;
  t->next = tracers;
  TRACER_SET(*flag, false);
  tracers = t;
}

static void add(const char *beg, const char *end, char ***ss, size_t *ns) {
  size_t n = *ns;
  size_t np = n + 1;
  char *s;
  size_t len;
  GPR_ASSERT(end >= beg);
  len = (size_t)(end - beg);
  s = (char *)gpr_malloc(len + 1);
  memcpy(s, beg, len);
  s[len] = 0;
  *ss = (char **)gpr_realloc(*ss, sizeof(char **) * np);
  (*ss)[n] = s;
  *ns = np;
}

static void split(const char *s, char ***ss, size_t *ns) {
  const char *c = strchr(s, ',');
  if (c == NULL) {
    add(s, s + strlen(s), ss, ns);
  } else {
    add(s, c, ss, ns);
    split(c + 1, ss, ns);
  }
}

static void parse(const char *s) {
  char **strings = NULL;
  size_t nstrings = 0;
  size_t i;
  split(s, &strings, &nstrings);

  for (i = 0; i < nstrings; i++) {
    if (strings[i][0] == '-') {
      grpc_tracer_set_enabled(strings[i] + 1, 0);
    } else {
      grpc_tracer_set_enabled(strings[i], 1);
    }
  }

  for (i = 0; i < nstrings; i++) {
    gpr_free(strings[i]);
  }
  gpr_free(strings);
}

static void list_tracers() {
  gpr_log(GPR_DEBUG, "available tracers:");
  tracer *t;
  for (t = tracers; t; t = t->next) {
    gpr_log(GPR_DEBUG, "\t%s", t->flag->name);
  }
}

void grpc_tracer_init(const char *env_var) {
  char *e = gpr_getenv(env_var);
  if (e != NULL) {
    parse(e);
    gpr_free(e);
  }
}

void grpc_tracer_shutdown(void) {
  while (tracers) {
    tracer *t = tracers;
    tracers = t->next;
    gpr_free(t);
  }
}

int grpc_tracer_set_enabled(const char *name, int enabled) {
  tracer *t;
  if (0 == strcmp(name, "all")) {
    for (t = tracers; t; t = t->next) {
      TRACER_SET(*t->flag, enabled);
    }
  } else if (0 == strcmp(name, "list_tracers")) {
    list_tracers();
  } else if (0 == strcmp(name, "refcount")) {
    for (t = tracers; t; t = t->next) {
      if (strstr(t->flag->name, "refcount") != NULL) {
        TRACER_SET(*t->flag, enabled);
      }
    }
  } else {
    int found = 0;
    for (t = tracers; t; t = t->next) {
      if (0 == strcmp(name, t->flag->name)) {
        TRACER_SET(*t->flag, enabled);
        found = 1;
      }
    }
    if (!found) {
      gpr_log(GPR_ERROR, "Unknown trace var: '%s'", name);
      return 0; /* early return */
    }
  }
  return 1;
}
