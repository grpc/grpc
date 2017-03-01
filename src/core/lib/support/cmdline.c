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

#include <grpc/support/cmdline.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/support/string.h"

typedef enum { ARGTYPE_INT, ARGTYPE_BOOL, ARGTYPE_STRING } argtype;

typedef struct arg {
  const char *name;
  const char *help;
  argtype type;
  void *value;
  struct arg *next;
} arg;

struct gpr_cmdline {
  const char *description;
  arg *args;
  const char *argv0;

  const char *extra_arg_name;
  const char *extra_arg_help;
  void (*extra_arg)(void *user_data, const char *arg);
  void *extra_arg_user_data;

  int (*state)(gpr_cmdline *cl, char *arg);
  arg *cur_arg;

  int survive_failure;
};

static int normal_state(gpr_cmdline *cl, char *arg);

gpr_cmdline *gpr_cmdline_create(const char *description) {
  gpr_cmdline *cl = gpr_zalloc(sizeof(gpr_cmdline));

  cl->description = description;
  cl->state = normal_state;

  return cl;
}

void gpr_cmdline_set_survive_failure(gpr_cmdline *cl) {
  cl->survive_failure = 1;
}

void gpr_cmdline_destroy(gpr_cmdline *cl) {
  while (cl->args) {
    arg *a = cl->args;
    cl->args = a->next;
    gpr_free(a);
  }
  gpr_free(cl);
}

static void add_arg(gpr_cmdline *cl, const char *name, const char *help,
                    argtype type, void *value) {
  arg *a;

  for (a = cl->args; a; a = a->next) {
    GPR_ASSERT(0 != strcmp(a->name, name));
  }

  a = gpr_zalloc(sizeof(arg));
  a->name = name;
  a->help = help;
  a->type = type;
  a->value = value;
  a->next = cl->args;
  cl->args = a;
}

void gpr_cmdline_add_int(gpr_cmdline *cl, const char *name, const char *help,
                         int *value) {
  add_arg(cl, name, help, ARGTYPE_INT, value);
}

void gpr_cmdline_add_flag(gpr_cmdline *cl, const char *name, const char *help,
                          int *value) {
  add_arg(cl, name, help, ARGTYPE_BOOL, value);
}

void gpr_cmdline_add_string(gpr_cmdline *cl, const char *name, const char *help,
                            char **value) {
  add_arg(cl, name, help, ARGTYPE_STRING, value);
}

void gpr_cmdline_on_extra_arg(
    gpr_cmdline *cl, const char *name, const char *help,
    void (*on_extra_arg)(void *user_data, const char *arg), void *user_data) {
  GPR_ASSERT(!cl->extra_arg);
  GPR_ASSERT(on_extra_arg);

  cl->extra_arg = on_extra_arg;
  cl->extra_arg_user_data = user_data;
  cl->extra_arg_name = name;
  cl->extra_arg_help = help;
}

/* recursively descend argument list, adding the last element
   to s first - so that arguments are added in the order they were
   added to the list by api calls */
static void add_args_to_usage(gpr_strvec *s, arg *a) {
  char *tmp;

  if (!a) return;
  add_args_to_usage(s, a->next);

  switch (a->type) {
    case ARGTYPE_BOOL:
      gpr_asprintf(&tmp, " [--%s|--no-%s]", a->name, a->name);
      gpr_strvec_add(s, tmp);
      break;
    case ARGTYPE_STRING:
      gpr_asprintf(&tmp, " [--%s=string]", a->name);
      gpr_strvec_add(s, tmp);
      break;
    case ARGTYPE_INT:
      gpr_asprintf(&tmp, " [--%s=int]", a->name);
      gpr_strvec_add(s, tmp);
      break;
  }
}

char *gpr_cmdline_usage_string(gpr_cmdline *cl, const char *argv0) {
  /* TODO(ctiller): make this prettier */
  gpr_strvec s;
  char *tmp;
  const char *name = strrchr(argv0, '/');

  if (name) {
    name++;
  } else {
    name = argv0;
  }

  gpr_strvec_init(&s);

  gpr_asprintf(&tmp, "Usage: %s", name);
  gpr_strvec_add(&s, tmp);
  add_args_to_usage(&s, cl->args);
  if (cl->extra_arg) {
    gpr_asprintf(&tmp, " [%s...]", cl->extra_arg_name);
    gpr_strvec_add(&s, tmp);
  }
  gpr_strvec_add(&s, gpr_strdup("\n"));

  tmp = gpr_strvec_flatten(&s, NULL);
  gpr_strvec_destroy(&s);
  return tmp;
}

static int print_usage_and_die(gpr_cmdline *cl) {
  char *usage = gpr_cmdline_usage_string(cl, cl->argv0);
  fprintf(stderr, "%s", usage);
  gpr_free(usage);
  if (!cl->survive_failure) {
    exit(1);
  }
  return 0;
}

static int extra_state(gpr_cmdline *cl, char *str) {
  if (!cl->extra_arg) {
    return print_usage_and_die(cl);
  }
  cl->extra_arg(cl->extra_arg_user_data, str);
  return 1;
}

static arg *find_arg(gpr_cmdline *cl, char *name) {
  arg *a;

  for (a = cl->args; a; a = a->next) {
    if (0 == strcmp(a->name, name)) {
      break;
    }
  }

  if (!a) {
    fprintf(stderr, "Unknown argument: %s\n", name);
    return NULL;
  }

  return a;
}

static int value_state(gpr_cmdline *cl, char *str) {
  long intval;
  char *end;

  GPR_ASSERT(cl->cur_arg);

  switch (cl->cur_arg->type) {
    case ARGTYPE_INT:
      intval = strtol(str, &end, 0);
      if (*end || intval < INT_MIN || intval > INT_MAX) {
        fprintf(stderr, "expected integer, got '%s' for %s\n", str,
                cl->cur_arg->name);
        return print_usage_and_die(cl);
      }
      *(int *)cl->cur_arg->value = (int)intval;
      break;
    case ARGTYPE_BOOL:
      if (0 == strcmp(str, "1") || 0 == strcmp(str, "true")) {
        *(int *)cl->cur_arg->value = 1;
      } else if (0 == strcmp(str, "0") || 0 == strcmp(str, "false")) {
        *(int *)cl->cur_arg->value = 0;
      } else {
        fprintf(stderr, "expected boolean, got '%s' for %s\n", str,
                cl->cur_arg->name);
        return print_usage_and_die(cl);
      }
      break;
    case ARGTYPE_STRING:
      *(char **)cl->cur_arg->value = str;
      break;
  }

  cl->state = normal_state;
  return 1;
}

static int normal_state(gpr_cmdline *cl, char *str) {
  char *eq = NULL;
  char *tmp = NULL;
  char *arg_name = NULL;
  int r = 1;

  if (0 == strcmp(str, "-help") || 0 == strcmp(str, "--help") ||
      0 == strcmp(str, "-h")) {
    return print_usage_and_die(cl);
  }

  cl->cur_arg = NULL;

  if (str[0] == '-') {
    if (str[1] == '-') {
      if (str[2] == 0) {
        /* handle '--' to move to just extra args */
        cl->state = extra_state;
        return 1;
      }
      str += 2;
    } else {
      str += 1;
    }
    /* first byte of str is now past the leading '-' or '--' */
    if (str[0] == 'n' && str[1] == 'o' && str[2] == '-') {
      /* str is of the form '--no-foo' - it's a flag disable */
      str += 3;
      cl->cur_arg = find_arg(cl, str);
      if (cl->cur_arg == NULL) {
        return print_usage_and_die(cl);
      }
      if (cl->cur_arg->type != ARGTYPE_BOOL) {
        fprintf(stderr, "%s is not a flag argument\n", str);
        return print_usage_and_die(cl);
      }
      *(int *)cl->cur_arg->value = 0;
      return 1; /* early out */
    }
    eq = strchr(str, '=');
    if (eq != NULL) {
      /* copy the string into a temp buffer and extract the name */
      tmp = arg_name = gpr_malloc((size_t)(eq - str + 1));
      memcpy(arg_name, str, (size_t)(eq - str));
      arg_name[eq - str] = 0;
    } else {
      arg_name = str;
    }
    cl->cur_arg = find_arg(cl, arg_name);
    if (cl->cur_arg == NULL) {
      return print_usage_and_die(cl);
    }
    if (eq != NULL) {
      /* str was of the type --foo=value, parse the value */
      r = value_state(cl, eq + 1);
    } else if (cl->cur_arg->type != ARGTYPE_BOOL) {
      /* flag types don't have a '--foo value' variant, other types do */
      cl->state = value_state;
    } else {
      /* flag parameter: just set the value */
      *(int *)cl->cur_arg->value = 1;
    }
  } else {
    r = extra_state(cl, str);
  }

  gpr_free(tmp);
  return r;
}

int gpr_cmdline_parse(gpr_cmdline *cl, int argc, char **argv) {
  int i;

  GPR_ASSERT(argc >= 1);
  cl->argv0 = argv[0];

  for (i = 1; i < argc; i++) {
    if (!cl->state(cl, argv[i])) {
      return 0;
    }
  }
  return 1;
}
