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

#ifndef GRPC_SUPPORT_CMDLINE_H
#define GRPC_SUPPORT_CMDLINE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Simple command line parser.

   Supports flags that can be specified as -foo, --foo, --no-foo, -no-foo, etc
   And integers, strings that can be specified as -foo=4, -foo blah, etc

   No support for short command line options (but we may get that in the
   future.)

   Usage (for a program with a single flag argument 'foo'):

   int main(int argc, char **argv) {
     gpr_cmdline *cl;
     int verbose = 0;

     cl = gpr_cmdline_create("My cool tool");
     gpr_cmdline_add_int(cl, "verbose", "Produce verbose output?", &verbose);
     gpr_cmdline_parse(cl, argc, argv);
     gpr_cmdline_destroy(cl);

     if (verbose) {
       gpr_log(GPR_INFO, "Goodbye cruel world!");
     }

     return 0;
   } */

typedef struct gpr_cmdline gpr_cmdline;

/* Construct a command line parser: takes a short description of the tool
   doing the parsing */
gpr_cmdline *gpr_cmdline_create(const char *description);
/* Add an integer parameter, with a name (used on the command line) and some
   helpful text (used in the command usage) */
void gpr_cmdline_add_int(gpr_cmdline *cl, const char *name, const char *help,
                         int *value);
/* The same, for a boolean flag */
void gpr_cmdline_add_flag(gpr_cmdline *cl, const char *name, const char *help,
                          int *value);
/* And for a string */
void gpr_cmdline_add_string(gpr_cmdline *cl, const char *name, const char *help,
                            char **value);
/* Set a callback for non-named arguments */
void gpr_cmdline_on_extra_arg(
    gpr_cmdline *cl, const char *name, const char *help,
    void (*on_extra_arg)(void *user_data, const char *arg), void *user_data);
/* Parse the command line */
void gpr_cmdline_parse(gpr_cmdline *cl, int argc, char **argv);
/* Destroy the parser */
void gpr_cmdline_destroy(gpr_cmdline *cl);
/* Get a string describing usage */
char *gpr_cmdline_usage_string(gpr_cmdline *cl, const char *argv0);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_SUPPORT_CMDLINE_H */
