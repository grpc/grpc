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

from pathlib import Path
from distutils import log

import pkg_resources
import setuptools


CONFIG_FILE_NAME = "pyproject.toml"
SECTION_NAME = "grpcio_tools"
PROTO_DIR_NAME = "protos"
PROTO_FILE_NAME = "route_guide.proto"
DEST_DIR_NAME = "my_module"


def generate_files(_: setuptools.Distribution) -> None:
    """
    Setuptools entrypoint for generating gRPC files based on prot files.

    :param _: Distribution being built(Unused)
    """
    log.warn("Generating GRPC Python files")
    config_file = Path(CONFIG_FILE_NAME)
    if not config_file.is_file():
        log.warn("No config. Doing nothing")
        return

    proto_include = pkg_resources.resource_filename("grpc_tools", "_proto")

    from grpc_tools import protoc

    result = protoc.main(
        [
            f"-I={proto_include}",
            f"-I={PROTO_DIR_NAME}",
            f"--python_out={DEST_DIR_NAME}",
            f"--grpc_python_out={DEST_DIR_NAME}",
            f"{PROTO_DIR_NAME}/{PROTO_FILE_NAME}",
        ]
    )

    if result == 0:
        log.warn("GRPC file generated")
    else:
        log.warn("GRPC file generation failed")
