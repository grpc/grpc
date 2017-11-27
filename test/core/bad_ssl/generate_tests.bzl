#!/usr/bin/env python2.7
# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


def test_options():
  return struct()


# maps test names to options
BAD_SSL_TESTS = ['cert', 'alpn']

def grpc_bad_ssl_tests():
  native.cc_library(
      name = 'bad_ssl_test_server',
      srcs = ['server_common.cc'],
      hdrs = ['server_common.h'],
      deps = ['//test/core/util:grpc_test_util', '//:grpc', '//test/core/end2end:ssl_test_data']
  )
  for t in BAD_SSL_TESTS:
    native.cc_test(
        name = 'bad_ssl_%s_server' % t,
        srcs = ['servers/%s.cc' % t],
        deps = [':bad_ssl_test_server'],
    )

