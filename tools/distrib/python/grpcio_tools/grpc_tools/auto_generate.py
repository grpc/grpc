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

from distutils import log
from pathlib import Path
import shlex
from typing import Any, Dict, Optional, NamedTuple

import pkg_resources
import setuptools

CONFIG_FILE_NAME = "pyproject.toml"
SECTION_NAME = "grpcio_tools"


# Could be switched to a dataclass once python 3.6 support is dropped.
class Configuration(NamedTuple):
    proto_dir: Path
    proto_file_pattern: str
    dest_dir: Path
    additional_args: Optional[str]


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
        args = [
            f"-I={proto_include}",
            f"-I={config.proto_dir}",
            f"--python_out={config.dest_dir}",
            f"--grpc_python_out={config.dest_dir}",
        ]
        if config.additional_args is not None:
            args.extend(shlex.split(config.additional_args))
        args.append(str(proto_file))

        result = protoc.main(args)
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
    # This implementation is complicated because tomli is listed as an option dependency. So the
    # code needs to be able to:
    #  * not cause any errors when the user did not configure auto-generation.
    #  * handle tomllib from the standard library
    #  * handle tomllib from the tomli
    #  * Cause an error when auto-generation is configured, but no tomllib is available.
    contents = config_file.read_text(encoding="utf-8")
    if "[tool.grpcio_tools]" not in contents:
        # user did not configure auto-generation
        return None

    try:
        import tomllib
    except ModuleNotFoundError:
        try:
            import tomli as tomllib
        except ModuleNotFoundError:
            raise RuntimeError(
              "setuptools auto-generation support requires Python 3.11 or tomli to be installed. "
              "Did you forget to use grpcio-tools[toml]?"
            )

    project_config = tomllib.loads(contents)

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
    additional_args = config_data.get("additional_args")

    if not isinstance(proto_dir, str):
        raise ValueError("'proto_dir' setting must be a string")
    if not isinstance(proto_file_pattern, str):
        raise ValueError("'proto_file_pattern' setting must be a string")
    if not isinstance(dest_dir, str):
        raise ValueError("'dest_dir' setting must be a string")
    if additional_args is not None and not isinstance(additional_args, str):
        raise ValueError("'additional_args' setting must be a string when provided")

    return Configuration(
        Path(proto_dir), proto_file_pattern, Path(dest_dir), additional_args
    )


class GenerationException(Exception):
    """An error occurred while generating content."""
