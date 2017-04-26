# Copyright 2016, Google Inc.
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

licenses(["notice"])  # 3-clause BSD

load(":generate_tests.bzl", "grpc_end2end_tests")

cc_library(
  name = 'cq_verifier',
  srcs = ['cq_verifier.c'],
  hdrs = ['cq_verifier.h'],
  deps = ['//:gpr', '//:grpc', '//test/core/util:grpc_test_util'],
  copts = ['-std=c99'],
  visibility = ["//test:__subpackages__"],
)

cc_library(
  name = 'ssl_test_data',
  visibility = ["//test:__subpackages__"],
  hdrs = ['data/ssl_test_data.h'],
  copts = ['-std=c99'],
  srcs = [
    "data/client_certs.c",
    "data/server1_cert.c",
    "data/server1_key.c",
    "data/test_root_cert.c",
  ]
)

cc_library(
  name = 'fake_resolver',
  hdrs = ['fake_resolver.h'],
  srcs = ['fake_resolver.c'],
  copts = ['-std=c99'],
  deps = ['//:gpr', '//:grpc', '//test/core/util:grpc_test_util']
)

cc_library(
  name = 'http_proxy',
  hdrs = ['fixtures/http_proxy_fixture.h'],
  srcs = ['fixtures/http_proxy_fixture.c'],
  copts = ['-std=c99'],
  deps = ['//:gpr', '//:grpc', '//test/core/util:grpc_test_util']
)

cc_library(
  name = 'proxy',
  hdrs = ['fixtures/proxy.h'],
  srcs = ['fixtures/proxy.c'],
  copts = ['-std=c99'],
  deps = ['//:gpr', '//:grpc', '//test/core/util:grpc_test_util']
)

grpc_end2end_tests()
