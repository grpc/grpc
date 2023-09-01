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
"""The module contains helpers to initialize and configure logging."""
import functools
import pathlib

from absl import flags
from absl import logging


def _ensure_flags_parsed() -> None:
    if not flags.FLAGS.is_parsed():
        raise flags.UnparsedFlagAccessError("Must initialize absl flags first.")


@functools.lru_cache(None)
def log_get_root_dir() -> pathlib.Path:
    _ensure_flags_parsed()
    log_root = pathlib.Path(logging.find_log_dir()).absolute()
    logging.info("Log root dir: %s", log_root)
    return log_root


def log_dir_mkdir(name: str) -> pathlib.Path:
    """Creates and returns a subdir with the given name in the log folder."""
    if len(pathlib.Path(name).parts) != 1:
        raise ValueError(f"Dir name must be a single component; got: {name}")
    if ".." in name:
        raise ValueError(f"Dir name must not be above the log root.")
    log_subdir = log_get_root_dir() / name
    if log_subdir.exists() and log_subdir.is_dir():
        logging.debug("Using existing log subdir: %s", log_subdir)
    else:
        log_subdir.mkdir()
        logging.debug("Created log subdir: %s", log_subdir)

    return log_subdir
