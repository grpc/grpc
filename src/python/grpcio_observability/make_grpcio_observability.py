#!/usr/bin/env python3

# Copyright 2023 gRPC authors.
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

from __future__ import print_function

import errno
import os
import os.path
import pprint
import shutil
import subprocess
import sys
import traceback

# the template for the content of observability_lib_deps.py
DEPS_FILE_CONTENT = """
# Copyright 2023 gRPC authors.
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

# AUTO-GENERATED BY make_grpcio_observability.py!
CC_INCLUDES={cc_includes}
"""

# expose commit hash suffix and prefix for check_grpcio_tools.py
COMMIT_HASH_PREFIX = 'PROTOBUF_SUBMODULE_VERSION="'
COMMIT_HASH_SUFFIX = '"'

EXTERNAL_LINKS = [
    ('@com_google_absl//', 'third_party/abseil-cpp/'),
    # ("@com_google_protobuf//", "third_party/protobuf/"),
    # ('@upb//:', 'third_party/upb/'),
#     ('@utf8_range//:', 'third_party/utf8_range/'),
]

ABSL_INCLUDE = (os.path.join("third_party", "abseil-cpp"),)
# CARES_INCLUDE = (
#     os.path.join("third_party", "cares", "cares", "include"),
#     os.path.join("third_party", "cares"),
#     os.path.join("third_party", "cares", "cares"),
# )
# if "darwin" in sys.platform:
#     CARES_INCLUDE += (os.path.join("third_party", "cares", "config_darwin"),)
# if "freebsd" in sys.platform:
#     CARES_INCLUDE += (os.path.join("third_party", "cares", "config_freebsd"),)
# if "linux" in sys.platform:
#     CARES_INCLUDE += (os.path.join("third_party", "cares", "config_linux"),)
# if "openbsd" in sys.platform:
#     CARES_INCLUDE += (os.path.join("third_party", "cares", "config_openbsd"),)
# UPB_INCLUDE = (os.path.join("third_party", "upb"),)
# UTF8_RANGE_INCLUDE = (os.path.join("third_party", "utf8_range"),)

# will be added to include path when building grpcio_observability
EXTENSION_INCLUDE_DIRECTORIES = (
    ABSL_INCLUDE
    # + CARES_INCLUDE
    # + UPB_INCLUDE
    # + UTF8_RANGE_INCLUDE
)

CC_INCLUDES = [
] + list(EXTENSION_INCLUDE_DIRECTORIES)

# the target directory is relative to the grpcio_observability package root.
GRPCIO_OBSERVABILITY_ROOT_PREFIX = 'src/python/grpcio_observability/'

# Pairs of (source, target) directories to copy
# from the grpc repo root to the grpcio_observability build root.
COPY_FILES_SOURCE_TARGET_PAIRS = [
    ('include', 'grpc_root/include'),
    ('third_party/abseil-cpp/absl', 'third_party/abseil-cpp/absl'),
    # ("third_party/cares/cares", "third_party/cares/cares"),
    # ('third_party/upb/upb', 'third_party/upb/upb'),
    # ("third_party/protobuf", "third_party/protobuf"),
    ('src/core/lib', 'grpc_root/src/core/lib'),
    # ('src/core/tsi/alts/handshaker', 'grpc_root/src/core/tsi/alts/handshaker'),
    # ('src/cpp/ext/filters/census', 'grpc_root/src/cpp/ext/filters/census'),
    # ('src/cpp/ext/gcp', 'grpc_root/src/cpp/ext/gcp'),
    # ('src/core/ext/upb-generated', 'grpc_root/src/core/ext/upb-generated'),
    # ('src/core/ext/filters/backend_metrics', 'grpc_root/src/core/ext/filters/backend_metrics'),
    ('src/core/ext/filters/client_channel/lb_policy', 'grpc_root/src/core/ext/filters/client_channel/lb_policy'),
    # ('src/core/ext/filters/census', 'grpc_root/src/core/ext/filters/census'),
    # ('src/cpp/filters/census', 'grpc_root/src/cpp/filters/census'),
]

# grpc repo root
GRPC_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))

# the directory under which to probe for the current protobuf commit SHA
# GRPC_PROTOBUF_SUBMODULE_ROOT = os.path.join(GRPC_ROOT, 'third_party',
#                                             'protobuf')

# the file to generate
GRPC_PYTHON_OBSERVABILITY_LIB_DEPS = os.path.join(GRPC_ROOT, 'src', 'python',
                                                  'grpcio_observability',
                                                  'observability_lib_deps.py')

# the script to run for getting dependencies
# BAZEL_DEPS = os.path.join(GRPC_ROOT, 'src', 'python', 'grpcio_observability',
#                           'bazel_deps.sh')

# # the bazel target to scrape to get list of sources for the build
# BAZEL_DEPS_QUERIES = [
#     # '//src/python/grpcio_observability/grpc_observability:observability',
#     '"//src/cpp/ext/gcp:observability_config"',
# ]

# def protobuf_submodule_commit_hash():
#     """Gets the commit hash for the HEAD of the protobuf submodule currently
#      checked out."""
#     cwd = os.getcwd()
#     os.chdir(GRPC_PROTOBUF_SUBMODULE_ROOT)
#     output = subprocess.check_output(['git', 'rev-parse', 'HEAD'])
#     os.chdir(cwd)
#     return output.decode("ascii").splitlines()[0].strip()

# def _bazel_query(query):
#     """Runs 'bazel query' to collect source file info."""
#     print('Running "bazel query %s"' % query)
#     output = subprocess.check_output([BAZEL_DEPS, query])
#     return output.decode("ascii").splitlines()


def _pretty_print_list(items):
    """Pretty print python list"""
    formatted = pprint.pformat(items, indent=4)
    # add newline after opening bracket (and fix indent of the next line)
    if formatted.startswith('['):
        formatted = formatted[0] + '\n ' + formatted[1:]
    # add newline before closing bracket
    if formatted.endswith(']'):
        formatted = formatted[:-1] + '\n' + formatted[-1]
    return formatted

# INTERNNEL_LINKS = [('//src', 'grpc_root/src'),
#                    ('//:src', 'grpc_root/src')]

# def _bazel_name_to_file_path(name):
#     """Transform bazel reference to source file name."""
#     for link in EXTERNAL_LINKS:
#         if name.startswith(link[0]):
#             filepath = link[1] + name[len(link[0]):].replace(':', '/')

#             # For some reason, the WKT sources (such as wrappers.pb.cc)
#             # end up being reported by bazel as having an extra 'wkt/google/protobuf'
#             # in path. Removing it makes the compilation pass.
#             # TODO(jtattermusch) Get dir of this hack.
#             return filepath.replace('wkt/google/protobuf/', '')
#     for link in INTERNNEL_LINKS:
#         if name.startswith(link[0]):
#             filepath = link[1] + name[len(link[0]):].replace(':', '/')
#             return filepath
#     return None


def _generate_deps_file_content():
    """Returns the data structure with dependencies of protoc as python code."""
    deps_file_content = DEPS_FILE_CONTENT.format(
        cc_includes=_pretty_print_list(CC_INCLUDES))
    return deps_file_content


def _copy_source_tree(source, target):
    """Copies source directory to a given target directory."""
    print('Copying contents of %s to %s' % (source, target))
    for source_dir, _, files in os.walk(source):
        target_dir = os.path.abspath(
            os.path.join(target, os.path.relpath(source_dir, source)))
        try:
            os.makedirs(target_dir)
        except OSError as error:
            if error.errno != errno.EEXIST:
                raise
        for relative_file in files:
            source_file = os.path.abspath(
                os.path.join(source_dir, relative_file))
            target_file = os.path.abspath(
                os.path.join(target_dir, relative_file))
            shutil.copyfile(source_file, target_file)


def main():
    os.chdir(GRPC_ROOT)

    # Step 1:
    # In order to be able to build the grpcio_observability package, we need the source code for the plugins
    # and its dependencies to be available under the build root of the grpcio_observability package.
    # So we simply copy all the necessary files where the build will expect them to be.
    for source, target in COPY_FILES_SOURCE_TARGET_PAIRS:
        # convert the slashes in the relative path to platform-specific path dividers.
        # All paths are relative to GRPC_ROOT
        source_abs = os.path.join(GRPC_ROOT, os.path.join(*source.split('/')))
        # for targets, add grpcio_observability root prefix
        target = GRPCIO_OBSERVABILITY_ROOT_PREFIX + target
        target_abs = os.path.join(GRPC_ROOT, os.path.join(*target.split('/')))
        _copy_source_tree(source_abs, target_abs)
    print('The necessary source files were copied under the grpcio_observability package root.')

    # Step 2:
    # Extract build metadata from bazel build (by running "bazel query")
    # and populate the observability_lib_deps.py file with python-readable data structure
    # that will be used by grpcio_observability's setup.py (so it knows how to configure
    # the native build for the codegen plugin)
    try:
        print('Invoking "bazel query" to gather the dependencies.')
        observability_lib_deps_content = _generate_deps_file_content()
    except Exception as error:
        # We allow this script to succeed even if we couldn't get the dependencies,
        # as then we can assume that even without a successful bazel run the
        # dependencies currently in source control are 'good enough'.
        sys.stderr.write("Got non-fatal error:\n")
        traceback.print_exc(file=sys.stderr)
        return
    # If we successfully got the dependencies, truncate and rewrite the deps file.
    with open(GRPC_PYTHON_OBSERVABILITY_LIB_DEPS, 'w') as deps_file:
        deps_file.write(observability_lib_deps_content)
    print('File "%s" updated.' % GRPC_PYTHON_OBSERVABILITY_LIB_DEPS)
    print('Done.')


if __name__ == '__main__':
    main()
