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
import shutil
import subprocess

# the target directory is relative to the grpcio_observability package root.
GRPCIO_OBSERVABILITY_ROOT_PREFIX = "src/python/grpcio_observability/"

# cygrpc.so build from bazel
# CYGRPC_SO_PATH = os.path.dirname(os.path.abspath(__file__))
# import sys; sys.stderr.write(f"CYGRPC_SO_PATH: {CYGRPC_SO_PATH}\n"); sys.stderr.flush()
# CYGRPC_SO_FILE = "_cygrpc.so"

import grpc
def _find_files_with_extension(directory: str, extension: str, name_only: bool = False):
    """Finds all files in a directory with the specified extension.

    Args:
        directory: The directory to search.
        extension: The file extension to find.
        name_only: Wether to only return file name.

    Returns:
        A list of file paths or file names that match the specified extension.
    """

    files = []
    for filename in os.listdir(directory):
        filepath = os.path.join(directory, filename)
        if os.path.isfile(filepath) and filename.endswith(extension):
            if name_only:
                files.append(filename)
            else:
                files.append(filepath)

    return files
print ("Checking path for cygrpc.so:")
print(grpc._cython.__path__)
CYGRPC_SO_PATH = os.path.realpath(grpc._cython.__path__[0])
print (f"CYGRPC_SO_PATH: {CYGRPC_SO_PATH}")
so_files = _find_files_with_extension(CYGRPC_SO_PATH, 'so', name_only=True)
CYGRPC_SO_FILE = so_files[0]
print (f"CYGRPC_SO_FILE: {CYGRPC_SO_FILE}")


# Pairs of (source, target) directories to copy
# from the grpc repo root to the grpcio_observability build root.
COPY_FILES_SOURCE_TARGET_PAIRS = [
    ("include", "grpc_root/include"),
    ("third_party/abseil-cpp/absl", "third_party/abseil-cpp/absl"),
    ("src/core/lib", "grpc_root/src/core/lib"),
    (
        "src/core/ext/filters/client_channel/lb_policy",
        "grpc_root/src/core/ext/filters/client_channel/lb_policy",
    ),
    ("src/cpp/ext/filters/census", "grpc_root/src/cpp/ext/filters/census"),
]

# grpc repo root
GRPC_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
)

# the script to run for getting dependencies
BAZEL_BUILD = os.path.join(
    GRPC_ROOT, "tools", "distrib", "python", "bazel_build.sh"
)


def _copy_source_tree(source, target):
    """Copies source directory to a given target directory."""
    print("Copying contents of %s to %s" % (source, target))
    for source_dir, _, files in os.walk(source):
        target_dir = os.path.abspath(
            os.path.join(target, os.path.relpath(source_dir, source))
        )
        try:
            os.makedirs(target_dir)
        except OSError as error:
            if error.errno != errno.EEXIST:
                raise
        for relative_file in files:
            source_file = os.path.abspath(
                os.path.join(source_dir, relative_file)
            )
            target_file = os.path.abspath(
                os.path.join(target_dir, relative_file)
            )
            shutil.copyfile(source_file, target_file)


def _bazel_build(query):
    """Runs 'bazel build' to build _cygrpc.so"""
    print('Running "bazel build %s"' % query)
    subprocess.check_output([BAZEL_BUILD, query])


def find_files_with_extension(directory: str, extension: str):
    """Finds all files in a directory with the specified extension.

    Args:
    directory: The directory to search.
    extension: The file extension to find.

    Returns:
    A list of file paths that match the specified extension.
    """

    files = []
    for filename in os.listdir(directory):
        filepath = os.path.join(directory, filename)
        if os.path.isfile(filepath) and filename.endswith(extension):
            files.append(filepath)

    return files

def main():
    os.chdir(GRPC_ROOT)

    # Step 1:
    # In order to be able to build the grpcio_observability package, we need the source code for the plugins
    # and its dependencies to be available under the build root of the grpcio_observability package.
    # So we simply copy all the necessary files where the build will expect them to be.
    for source, target in COPY_FILES_SOURCE_TARGET_PAIRS:
        # convert the slashes in the relative path to platform-specific path dividers.
        # All paths are relative to GRPC_ROOT
        source_abs = os.path.join(GRPC_ROOT, os.path.join(*source.split("/")))
        # for targets, add grpcio_observability root prefix
        target = GRPCIO_OBSERVABILITY_ROOT_PREFIX + target
        target_abs = os.path.join(GRPC_ROOT, os.path.join(*target.split("/")))
        _copy_source_tree(source_abs, target_abs)
    print("The necessary source files were copied under"
          + " the grpcio_observability package root.")

    # Step 2:
    # Use bazel to build _cygrpc.so and save it to proper location.
    # _bazel_build("//src/python/grpcio/grpc/_cython:cygrpc")

    # _source_dir = os.path.join(
    #     GRPC_ROOT,
    #     "bazel-bin",
    #     "src",
    #     "python",
    #     "grpcio",
    #     "grpc",
    #     "_cython",
    #     "cygrpc.so",
    # )
    # source_file = os.path.abspath(_source_dir)
    # target_file = os.path.abspath(os.path.join(CYGRPC_SO_PATH, CYGRPC_SO_FILE))
    # print("Copying %s to %s" % (source_file, target_file))
    # shutil.copyfile(source_file, target_file)


if __name__ == "__main__":
    main()
