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

#
# This is for the gRPC build system. This isn't intended to be used outsite of
# the BUILD file for gRPC. It contains the mapping for the template system we
# use to generate other platform's build system files.
#

def grpc_cc_library(name, srcs = [], public_hdrs = [], hdrs = [], external_deps = [], deps = [], standalone = False, language = "C++"):
  copts = []
  if language.upper() == "C":
    copts = ["-std=c99"]
  native.cc_library(
    name = name,
    srcs = srcs,
    hdrs = hdrs + public_hdrs,
    deps = deps + ["//external:" + dep for dep in external_deps],
    copts = copts,
    linkopts = ["-pthread"],
    includes = [
        "include"
    ]
  )

def grpc_proto_plugin(name, srcs = [], deps = []):
  native.cc_binary(
    name = name,
    srcs = srcs,
    deps = deps,
  )

load("//:bazel/cc_grpc_library.bzl", "cc_grpc_library")

def grpc_proto_library(name, srcs = [], deps = [], well_known_deps = [], has_services = True):
  cc_grpc_library(
    name = name,
    srcs = srcs,
    deps = deps,
    proto_only = not has_services,
  )

