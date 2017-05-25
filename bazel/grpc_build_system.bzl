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

#
# This is for the gRPC build system. This isn't intended to be used outsite of
# the BUILD file for gRPC. It contains the mapping for the template system we
# use to generate other platform's build system files.
#
# Please consider that there should be a high bar for additions and changes to
# this file.
# Each rule listed must be re-written for Google's internal build system, and
# each change must be ported from one to the other.
#

def grpc_cc_library(name, srcs = [], public_hdrs = [], hdrs = [],
                    external_deps = [], deps = [], standalone = False,
                    language = "C++", testonly = False, visibility = None):
  copts = []
  if language.upper() == "C":
    copts = ["-std=c99"]
  native.cc_library(
    name = name,
    srcs = srcs,
    hdrs = hdrs + public_hdrs,
    deps = deps + ["//external:" + dep for dep in external_deps],
    copts = copts,
    visibility = visibility,
    testonly = testonly,
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

def grpc_proto_library(name, srcs = [], deps = [], well_known_protos = False,
                       has_services = True, use_external = False, generate_mock = False):
  cc_grpc_library(
    name = name,
    srcs = srcs,
    deps = deps,
    well_known_protos = well_known_protos,
    proto_only = not has_services,
    use_external = use_external,
    generate_mock = generate_mock,
  )

def grpc_cc_test(name, srcs = [], deps = [], external_deps = [], args = [], data = [], language = "C++"):
  copts = []
  if language.upper() == "C":
    copts = ["-std=c99"]
  native.cc_test(
    name = name,
    srcs = srcs,
    args = args,
    data = data,
    deps = deps + ["//external:" + dep for dep in external_deps],
    copts = copts,
    linkopts = ["-pthread"],
  )

def grpc_cc_binary(name, srcs = [], deps = [], external_deps = [], args = [], data = [], language = "C++", testonly = False, linkshared = False):
  copts = []
  if language.upper() == "C":
    copts = ["-std=c99"]
  native.cc_binary(
    name = name,
    srcs = srcs,
    args = args,
    data = data,
    testonly = testonly,
    linkshared = linkshared,
    deps = deps + ["//external:" + dep for dep in external_deps],
    copts = copts,
    linkopts = ["-pthread"],
  )

def grpc_generate_one_off_targets():
  pass

def grpc_sh_test(name, srcs, args = [], data = []):
  native.sh_test(
    name = name,
    srcs = srcs,
    args = args,
    data = data)
