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
from typing import Any, Dict, Optional, NamedTuple

import pkg_resources
import setuptools

try:
    import tomllib
except ModuleNotFoundError:
    import tomli as tomllib


CONFIG_FILE_NAME = "pyproject.toml"
SECTION_NAME = "grpcio_tools"


# Could be switched to a dataclass once python 3.6 support is dropped.
class Configuration(NamedTuple):
    proto_dir: Path
    proto_file_pattern: str
    dest_dir: Path


def generate_files(_: setuptools.Distribution) -> None:
    """
    Setuptools entrypoint for generating gRPC files based on prot files.

    :param _: Distribution being built(Unused)
    """
    log.warn("Generating GRPC Python files")
    config_file = Path(CONFIG_FILE_NAME)
    if not config_file.is_file():
        log.info("No config. Doing nothing")
        return
    config = _read_config(config_file)
    if config is None:
        log.info(f"No {SECTION_NAME} in {CONFIG_FILE_NAME}. Doing nothing")
        return

    proto_include = pkg_resources.resource_filename("grpc_tools", "_proto")

    from grpc_tools import protoc

    generation_executed = False
    for proto_file in config.proto_dir.glob(config.proto_file_pattern):
        result = protoc.main(
            [
                f"-I={proto_include}",
                f"-I={config.proto_dir}",
                f"--python_out={config.dest_dir}",
                f"--grpc_python_out={config.dest_dir}",
                str(proto_file),
            ]
        )
        generation_executed = True

        if result != 0:
            raise GenerationException(f"Failed to generate content for {proto_file}")

    if not generation_executed:
        raise GenerationException("No matching proto files")


def _read_config(config_file: Path) -> Optional[Configuration]:
    """
    Retrieve the auto-generate configuration values.

    :param config_file: File containing the configuration values.

    :return: Configuration settings as it appeared in the configuration file.
        `None` if the expected section did not exist.
    :raises ValueError: The configuration file is not correctly formatted.
    :raises OSError: There was a problem reading the configuration file.
    """
    with open(config_file, "rb") as f:
        project_config = tomllib.load(f)

    try:
        section_config = project_config["tool"][SECTION_NAME]
    except KeyError:
        return None

    return _parse_config(section_config)


def _parse_config(config_data: Dict[str, Any]) -> Configuration:
    """
    Parse the configuration data into a structured form.

    :param config_data: Configuration that was read from the file.
    :return:
    """
    try:
        proto_dir = config_data["proto_dir"]
        proto_file_pattern = config_data["proto_file_pattern"]
        dest_dir = config_data["dest_dir"]
    except KeyError as ex:
        raise ValueError("Missing setting in configuration section") from ex

    if not isinstance(proto_dir, str):
        raise ValueError("'proto_dir' setting must be a string")
    if not isinstance(proto_file_pattern, str):
        raise ValueError("'proto_file_pattern' setting must be a string")
    if not isinstance(dest_dir, str):
        raise ValueError("'dest_dir' setting must be a string")

    return Configuration(Path(proto_dir), proto_file_pattern, Path(dest_dir))


class GenerationException(Exception):
    """An error occurred while generating content."""
