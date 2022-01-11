# Copyright 2022 gRPC authors.
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

def external_proto_library(name, dep):
    """Creates libraries from an external proto_library target meant to be used as a dependency in grpc_proto_library
    Args:
      name: The name of the target
      dep: external proto_library target
    """
    proto_target = "_" + name + "_only"
    native.alias(
        name = proto_target,
        actual = dep,
    )

    native.cc_proto_library(
        name = name,
        deps = [":" + proto_target],
    )
