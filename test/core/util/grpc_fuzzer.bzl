# Copyright 2016 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")

def grpc_fuzzer(name, corpus, srcs = [], deps = [], **kwargs):
  grpc_cc_test(
    name = name,
    srcs = srcs,
    deps = deps + ["//test/core/util:fuzzer_corpus_test"],
    data = native.glob([corpus + "/**"]),
    external_deps = [
      'gtest',
    ],
    args = ["--directory=" + native.package_name() + "/" + corpus,],
    **kwargs
  )
