/*
 *
 * Copyright 2017 gRPC authors.
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

/*******************************************************************************
 * This test verifies that various stack configurations result in the set of
 * filters that we expect.
 *
 * This is akin to a golden-file test, and suffers the same disadvantages and
 * advantages: it reflects that the code as written has not been modified - and
 * valid code modifications WILL break this test and it will need updating.
 *
 * The intent therefore is to allow code reviewers to more easily catch changes
 * that perturb the generated list of channel filters in different
 * configurations and assess whether such a change is correct and desirable.
 */

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/support/string.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport_impl.h"
#include "test/core/util/test_config.h"

// use CHECK_STACK instead
static int check_stack(const char *file, int line, const char *transport_name,
                       grpc_channel_args *init_args,
                       unsigned channel_stack_type, ...);

// arguments: const char *transport_name   - the name of the transport type to
//                                           simulate
//            grpc_channel_args *init_args - channel args to pass down
//            grpc_channel_stack_type channel_stack_type - the archetype of
//                                           channel stack to create
//            variadic arguments - the (in-order) expected list of channel
//                                 filters to instantiate, terminated with NULL
#define CHECK_STACK(...) check_stack(__FILE__, __LINE__, __VA_ARGS__)

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  int errors = 0;

  // tests with a minimal stack
  grpc_arg minimal_stack_arg = {.type = GRPC_ARG_INTEGER,
                                .key = GRPC_ARG_MINIMAL_STACK,
                                .value.integer = 1};
  grpc_channel_args minimal_stack_args = {.num_args = 1,
                                          .args = &minimal_stack_arg};
  errors += CHECK_STACK("unknown", &minimal_stack_args,
                        GRPC_CLIENT_DIRECT_CHANNEL, "connected", NULL);
  errors += CHECK_STACK("unknown", &minimal_stack_args, GRPC_CLIENT_SUBCHANNEL,
                        "connected", NULL);
  errors += CHECK_STACK("unknown", &minimal_stack_args, GRPC_SERVER_CHANNEL,
                        "server", "connected", NULL);
  errors +=
      CHECK_STACK("chttp2", &minimal_stack_args, GRPC_CLIENT_DIRECT_CHANNEL,
                  "http-client", "connected", NULL);
  errors += CHECK_STACK("chttp2", &minimal_stack_args, GRPC_CLIENT_SUBCHANNEL,
                        "http-client", "connected", NULL);
  errors += CHECK_STACK("chttp2", &minimal_stack_args, GRPC_SERVER_CHANNEL,
                        "server", "http-server", "connected", NULL);
  errors += CHECK_STACK(NULL, &minimal_stack_args, GRPC_CLIENT_CHANNEL,
                        "client-channel", NULL);

  // tests with a default stack
  errors += CHECK_STACK("unknown", NULL, GRPC_CLIENT_DIRECT_CHANNEL,
                        "message_size", "deadline", "connected", NULL);
  errors += CHECK_STACK("unknown", NULL, GRPC_CLIENT_SUBCHANNEL, "message_size",
                        "connected", NULL);
  errors += CHECK_STACK("unknown", NULL, GRPC_SERVER_CHANNEL, "server",
                        "message_size", "deadline", "connected", NULL);
  errors +=
      CHECK_STACK("chttp2", NULL, GRPC_CLIENT_DIRECT_CHANNEL, "message_size",
                  "deadline", "http-client", "compress", "connected", NULL);
  errors += CHECK_STACK("chttp2", NULL, GRPC_CLIENT_SUBCHANNEL, "message_size",
                        "http-client", "compress", "connected", NULL);
  errors +=
      CHECK_STACK("chttp2", NULL, GRPC_SERVER_CHANNEL, "server", "message_size",
                  "deadline", "http-server", "compress", "connected", NULL);
  errors +=
      CHECK_STACK(NULL, NULL, GRPC_CLIENT_CHANNEL, "client-channel", NULL);

  GPR_ASSERT(errors == 0);
  grpc_shutdown();
  return 0;
}

/*******************************************************************************
 * End of tests definitions, start of test infrastructure
 */

static int check_stack(const char *file, int line, const char *transport_name,
                       grpc_channel_args *init_args,
                       unsigned channel_stack_type, ...) {
  // create dummy channel stack
  grpc_channel_stack_builder *builder = grpc_channel_stack_builder_create();
  grpc_transport_vtable fake_transport_vtable = {.name = transport_name};
  grpc_transport fake_transport = {.vtable = &fake_transport_vtable};
  grpc_channel_stack_builder_set_target(builder, "foo.test.google.fr");
  grpc_channel_args *channel_args = grpc_channel_args_copy(init_args);
  if (transport_name != NULL) {
    grpc_channel_stack_builder_set_transport(builder, &fake_transport);
  }
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_stack_builder_set_channel_arguments(&exec_ctx, builder,
                                                     channel_args);
    GPR_ASSERT(grpc_channel_init_create_stack(
        &exec_ctx, builder, (grpc_channel_stack_type)channel_stack_type));
    grpc_exec_ctx_finish(&exec_ctx);
  }

  // build up our expectation list
  gpr_strvec v;
  gpr_strvec_init(&v);
  va_list args;
  va_start(args, channel_stack_type);
  for (;;) {
    char *a = va_arg(args, char *);
    if (a == NULL) break;
    if (v.count != 0) gpr_strvec_add(&v, gpr_strdup(", "));
    gpr_strvec_add(&v, gpr_strdup(a));
  }
  va_end(args);
  char *expect = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);

  // build up our "got" list
  gpr_strvec_init(&v);
  grpc_channel_stack_builder_iterator *it =
      grpc_channel_stack_builder_create_iterator_at_first(builder);
  while (grpc_channel_stack_builder_move_next(it)) {
    const char *name = grpc_channel_stack_builder_iterator_filter_name(it);
    if (name == NULL) continue;
    if (v.count != 0) gpr_strvec_add(&v, gpr_strdup(", "));
    gpr_strvec_add(&v, gpr_strdup(name));
  }
  char *got = gpr_strvec_flatten(&v, NULL);
  gpr_strvec_destroy(&v);
  grpc_channel_stack_builder_iterator_destroy(it);

  // figure out result, log if there's an error
  int result = 0;
  if (0 != strcmp(got, expect)) {
    gpr_strvec_init(&v);
    gpr_strvec_add(&v, gpr_strdup("{"));
    for (size_t i = 0; i < channel_args->num_args; i++) {
      if (i > 0) gpr_strvec_add(&v, gpr_strdup(", "));
      gpr_strvec_add(&v, gpr_strdup(channel_args->args[i].key));
      gpr_strvec_add(&v, gpr_strdup("="));
      switch (channel_args->args[i].type) {
        case GRPC_ARG_INTEGER: {
          char *tmp;
          gpr_asprintf(&tmp, "%d", channel_args->args[i].value.integer);
          gpr_strvec_add(&v, tmp);
          break;
        }
        case GRPC_ARG_STRING:
          gpr_strvec_add(&v, gpr_strdup(channel_args->args[i].value.string));
          break;
        case GRPC_ARG_POINTER: {
          char *tmp;
          gpr_asprintf(&tmp, "%p", channel_args->args[i].value.pointer.p);
          gpr_strvec_add(&v, tmp);
          break;
        }
      }
    }
    gpr_strvec_add(&v, gpr_strdup("}"));
    char *args_str = gpr_strvec_flatten(&v, NULL);
    gpr_strvec_destroy(&v);

    gpr_log(file, line, GPR_LOG_SEVERITY_ERROR,
            "**************************************************");
    gpr_log(file, line, GPR_LOG_SEVERITY_ERROR,
            "FAILED transport=%s; stack_type=%s; channel_args=%s:",
            transport_name, grpc_channel_stack_type_string(channel_stack_type),
            args_str);
    gpr_log(file, line, GPR_LOG_SEVERITY_ERROR, "EXPECTED: %s", expect);
    gpr_log(file, line, GPR_LOG_SEVERITY_ERROR, "GOT:      %s", got);
    result = 1;

    gpr_free(args_str);
  }

  gpr_free(got);
  gpr_free(expect);

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_stack_builder_destroy(&exec_ctx, builder);
    grpc_channel_args_destroy(&exec_ctx, channel_args);
    grpc_exec_ctx_finish(&exec_ctx);
  }

  return result;
}
