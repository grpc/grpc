#!/usr/bin/env python2.7
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


"""Generates the appropriate build.json data for all the bad_client tests."""


def test_options():
  return struct()


# maps test names to options
BAD_CLIENT_TESTS = {
    'badreq': test_options(),
    'connection_prefix': test_options(),
    'headers': test_options(),
    'initial_settings_frame': test_options(),
    'head_of_line_blocking': test_options(),
    'large_metadata': test_options(),
    'server_registered_method': test_options(),
    'simple_request': test_options(),
    'window_overflow': test_options(),
    'unknown_frame': test_options(),
}

def grpc_bad_client_tests():
  native.cc_library(
      name = 'bad_client_test',
      srcs = ['bad_client.c'],
      hdrs = ['bad_client.h'],
      copts = ['-std=c99'],
      deps = ['//test/core/util:grpc_test_util', '//:grpc', '//:gpr', '//test/core/end2end:cq_verifier']
  )
  for t, topt in BAD_CLIENT_TESTS.items():
    native.cc_test(
        name = '%s_bad_client_test' % t,
        srcs = ['tests/%s.c' % t],
        deps = [':bad_client_test'],
        copts = ['-std=c99'],
    )

