# Copyright 2019 The gRPC Authors
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

import os
import site
import sys

_GRPC_BAZEL_RUNTIME_ENV = "GRPC_BAZEL_RUNTIME"


# TODO(https://github.com/bazelbuild/bazel/issues/6844) Bazel failed to
# interpret namespace packages correctly. This monkey patch will force the
# Python process to parse the .pth file in the sys.path to resolve namespace
# package in the right place.
# Analysis in depth: https://github.com/bazelbuild/rules_python/issues/55
def sys_path_to_site_dir_hack():
    """Add valid sys.path item to site directory to parse the .pth files."""
    # Only run within our Bazel environment
    if not os.environ.get(_GRPC_BAZEL_RUNTIME_ENV):
        return
    items = []
    for item in sys.path:
        if os.path.exists(item):
            # The only difference between sys.path and site-directory is
            # whether the .pth file will be parsed or not. A site-directory
            # will always exist in sys.path, but not another way around.
            items.append(item)
    for item in items:
        site.addsitedir(item)
