# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Load and parse .pyc files."""

import io

from typing import IO, Union

from . import magic
from . import marshal


def load(fi: IO[bytes]):
    """Parse pyc data from a stream.

    Args:
      fi: A file-like object.

    Returns:
      An instance of types.CodeTypeBase.

    Raises:
      IOError: If we can't read the file or the file is malformed.
    """
    magic_number = fi.read(2)
    python_version = magic.magic_number_to_version(magic_number)
    crlf = fi.read(2)  # cr, lf
    if crlf != b"\r\n":
        raise OSError("Malformed pyc file")
    fi.read(12)  # skip rest of header
    return marshal.loads(fi.read(), python_version)


def loads(data: Union[bytes, str]):
    """Parse pyc data from a string.

    Args:
      data: pyc data

    Returns:
      An instance of types.CodeTypeBase.
    """
    return load(io.BytesIO(data))


def load_file(path: str):
    """Parse pyc data from a file.

    Args:
      path: A file path.

    Returns:
      An instance of types.CodeTypeBase.

    Raises:
      IOError: If we can't read the file or the file is malformed.
    """
    with open(path, "rb") as f:
        return load(f)
