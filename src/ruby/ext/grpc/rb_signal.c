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

#include <ruby/ruby.h>
#include <signal.h>
#include <stdbool.h>

#include <grpc/support/log.h>

#include "rb_grpc.h"

static void (*old_sigint_handler)(int);
static void (*old_sigterm_handler)(int);

static volatile bool signal_received = false;

static void handle_signal(int signum) {
  signal_received = true;
  if (signum == SIGINT) {
    old_sigint_handler(signum);
  } else if (signum == SIGTERM) {
    old_sigterm_handler(signum);
  }
}

static VALUE grpc_rb_signal_received(VALUE self) {
  (void)self;
  return signal_received ? Qtrue : Qfalse;
}

void Init_grpc_signals() {
  old_sigint_handler = signal(SIGINT, handle_signal);
  old_sigterm_handler = signal(SIGTERM, handle_signal);
  rb_define_singleton_method(grpc_rb_mGrpcCore, "signal_received?",
                             grpc_rb_signal_received, 0);
}
