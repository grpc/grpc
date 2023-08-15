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

# the target directory is relative to the grpcio_observability package root.
GRPCIO_OBSERVABILITY_ROOT_PREFIX = 'src/python/grpcio_observability/'

# Pairs of (source, target) directories to copy
# from the grpc repo root to the grpcio_observability build root.
COPY_FILES_SOURCE_TARGET_PAIRS = [
    ('include', 'grpc_root/include'),
    ('third_party/abseil-cpp/absl', 'third_party/abseil-cpp/absl'),
    ('src/core/lib', 'grpc_root/src/core/lib'),
    ('src/core/ext/filters/client_channel/lb_policy', 'grpc_root/src/core/ext/filters/client_channel/lb_policy'),
]

# grpc repo root
GRPC_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))

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


if __name__ == '__main__':
    main()
