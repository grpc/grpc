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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary")

def grpc_fuzzer(name, corpus, srcs = [], deps = [], **kwargs):
  grpc_cc_binary(
    name = '%s/one_entry.bin' % name,
    srcs = srcs,
    deps = deps + ["//test/core/util:one_corpus_entry_fuzzer"],
    **kwargs
  )
  for entry in native.glob(['%s/*' % corpus]):
    native.sh_test(
      name = '%s/one_entry/%s' % (name, entry),
      data = [':%s/one_entry.bin' % name, entry],
      srcs = ['//test/core/util:fuzzer_one_entry_runner'],
      args = ['$(location :%s/one_entry.bin)' % name, '$(location %s)' % entry]
    )
