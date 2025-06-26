# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import os
from contextlib import contextmanager
from pathlib import Path
from typing import Generator

from libcst._types import StrPath


@contextmanager
def chdir(path: StrPath) -> Generator[Path, None, None]:
    """
    Temporarily chdir to the given path, and then return to the previous path.
    """
    try:
        path = Path(path).resolve()
        cwd = os.getcwd()
        os.chdir(path)
        yield path
    finally:
        os.chdir(cwd)
