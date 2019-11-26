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


import importlib
import os
from .protoc import main

# TODO: Get this thing to just give me the code via an FD.
# TODO: Figure out what to do about STDOUT pollution.
# TODO: Search sys.path to figure out project_root automatically?
def import_protos(proto_path, project_root):
    proto_basename = os.path.basename(proto_path)
    proto_name, _ = os.path.splitext(proto_basename)
    anchor_package = ".".join(os.path.normpath(os.path.dirname(proto_path)).split(os.sep))
    original_dir = os.getcwd()
    try:
        os.chdir(os.path.join(original_dir, project_root))
        return_value = protoc.main([
          "grpc_tools.protoc",
          "--proto_path=.",
          "--python_out=.",
          "--grpc_python_out=.",
          proto_path
        ])
    finally:
        os.chdir(original_dir)
    if return_value != 0:
      raise RuntimeError("Protoc failed.")
    print("anchor_package: {}".format(anchor_package))
    protos = importlib.import_module("{}.{}_pb2".format(anchor_package, proto_name))
    services = importlib.import_module("{}.{}_pb2_grpc".format(anchor_package, proto_name))
    return protos, services

