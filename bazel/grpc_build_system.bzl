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

# The set of pollers to test against if a test exercises polling
POLLERS = ['epollex', 'epollsig', 'epoll1', 'poll', 'poll-cv']

def if_not_windows(a):
  return select({
      "//:windows": [],
      "//:windows_msvc": [],
      "//conditions:default": a,
  })

def _get_external_deps(external_deps):
  ret = []
  for dep in external_deps:
    if dep == "nanopb":
      ret += ["grpc_nanopb"]
    elif dep == "address_sorting":
      ret += ["//third_party/address_sorting"]
    elif dep == "cares":
      ret += select({"//:grpc_no_ares": [],
                     "//conditions:default": ["//external:cares"],})
    else:
      ret += ["//external:" + dep]
  return ret

def _maybe_update_cc_library_hdrs(hdrs):
  ret = []
  hdrs_to_update = {
      "third_party/objective_c/Cronet/bidirectional_stream_c.h": "//third_party:objective_c/Cronet/bidirectional_stream_c.h",
  }
  for h in hdrs:
    if h in hdrs_to_update.keys():
      ret.append(hdrs_to_update[h])
    else:
      ret.append(h)
  return ret

def grpc_cc_library(name, srcs = [], public_hdrs = [], hdrs = [],
                    external_deps = [], deps = [], standalone = False,
                    language = "C++", testonly = False, visibility = None,
                    alwayslink = 0):
  copts = []
  if language.upper() == "C":
    copts = if_not_windows(["-std=c99"])
  native.cc_library(
    name = name,
    srcs = srcs,
    defines = select({"//:grpc_no_ares": ["GRPC_ARES=0"],
                      "//conditions:default": [],}) +
              select({"//:remote_execution":  ["GRPC_PORT_ISOLATED_RUNTIME=1"],
                      "//conditions:default": [],}) +
              select({"//:grpc_allow_exceptions":  ["GRPC_ALLOW_EXCEPTIONS=1"],
                      "//:grpc_disallow_exceptions":
                      ["GRPC_ALLOW_EXCEPTIONS=0"],
                      "//conditions:default": [],}),
    hdrs = _maybe_update_cc_library_hdrs(hdrs + public_hdrs),
    deps = deps + _get_external_deps(external_deps),
    copts = copts,
    visibility = visibility,
    testonly = testonly,
    linkopts = if_not_windows(["-pthread"]),
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
                       has_services = True, use_external = False, generate_mocks = False):
  cc_grpc_library(
    name = name,
    srcs = srcs,
    deps = deps,
    well_known_protos = well_known_protos,
    proto_only = not has_services,
    use_external = use_external,
    generate_mocks = generate_mocks,
  )

def grpc_cc_test(name, srcs = [], deps = [], external_deps = [], args = [], data = [], uses_polling = True, language = "C++", size = "medium", timeout = "moderate"):
  copts = []
  if language.upper() == "C":
    copts = if_not_windows(["-std=c99"])
  args = {
    'name': name,
    'srcs': srcs,
    'args': args,
    'data': data,
    'deps': deps + _get_external_deps(external_deps),
    'copts': copts,
    'linkopts': if_not_windows(["-pthread"]),
    'size': size,
    'timeout': timeout,
  }
  if uses_polling:
    native.cc_test(testonly=True, tags=['manual'], **args)
    for poller in POLLERS:
      native.sh_test(
        name = name + '@poller=' + poller,
        data = [name],
        srcs = [
          '//test/core/util:run_with_poller_sh',
        ],
        size = size,
        timeout = timeout,
        args = [
          poller,
          '$(location %s)' % name,
        ] + args['args'],
      )
  else:
    native.cc_test(**args)

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
    deps = deps + _get_external_deps(external_deps),
    copts = copts,
    linkopts = if_not_windows(["-pthread"]) + linkopts,
  )

def grpc_generate_one_off_targets():
  native.cc_library(
    name = "grpc_nanopb",
    hdrs = [
      "//third_party/nanopb:pb.h",
      "//third_party/nanopb:pb_common.h",
      "//third_party/nanopb:pb_decode.h",
      "//third_party/nanopb:pb_encode.h",
    ],
    srcs = [
      "//third_party/nanopb:pb_common.c",
      "//third_party/nanopb:pb_decode.c",
      "//third_party/nanopb:pb_encode.c",
    ],
    defines = [
      "PB_FIELD_16BIT=1",
    ],
  )

def grpc_sh_test(name, srcs, args = [], data = []):
  native.sh_test(
    name = name,
    srcs = srcs,
    args = args,
    data = data)

def grpc_sh_binary(name, srcs, data = []):
  native.sh_binary(
    name = name,
    srcs = srcs,
    data = data)

def grpc_py_binary(name, srcs, data = [], deps = [], external_deps = [], testonly = False):
  native.py_binary(
    name = name,
    srcs = srcs,
    testonly = testonly,
    data = data,
    deps = deps + _get_external_deps(external_deps)
  )

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
