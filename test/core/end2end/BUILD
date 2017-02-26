# Copyright 2016, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

licenses(["notice"])  # Apache v2

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
  hdrs = ['fixtures/http_proxy.h'],
  srcs = ['fixtures/http_proxy.c'],
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
