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
                    language = "C++", testonly = False, visibility = None,
                    alwayslink = 0):
  copts = []
  if language.upper() == "C":
    copts = ["-std=c99"]
  native.cc_library(
    name = name,
    srcs = srcs,
    defines = select({
        "//:grpc_no_ares": ["GRPC_ARES=0"],
	"//conditions:default": [],
    }),
    hdrs = hdrs + public_hdrs,
    deps = deps + ["//external:" + dep for dep in external_deps],
    copts = copts,
    visibility = visibility,
    testonly = testonly,
    linkopts = ["-pthread"],
    includes = [
        "include"
    ],
    alwayslink = alwayslink,
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

def grpc_cc_binary(name, srcs = [], deps = [], external_deps = [], args = [], data = [], language = "C++", testonly = False, linkshared = False, linkopts = []):
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
    linkopts = ["-pthread"] + linkopts,
  )

def grpc_generate_one_off_targets():
  pass

def grpc_sh_test(name, srcs, args = [], data = []):
  native.sh_test(
    name = name,
    srcs = srcs,
    args = args,
    data = data)

def grpc_sh_binary(name, srcs, data = []):
  native.sh_test(
    name = name,
    srcs = srcs,
    data = data)

def grpc_py_binary(name, srcs, data = [], deps = []):
  if name == "test_dns_server":
    # TODO: allow running test_dns_server in oss bazel test suite
    deps = []
  native.py_binary(
    name = name,
    srcs = srcs,
    data = data,
    deps = deps)

def grpc_package(name, visibility = "private", features = []):
  if visibility == "tests":
    visibility = ["//test:__subpackages__"]
  elif visibility == "public":
    visibility = ["//visibility:public"]
  elif visibility == "private":
    visibility = []
  else:
    fail("Unknown visibility " + visibility)

  if len(visibility) != 0:
    native.package(
      default_visibility = visibility,
      features = features
    )
