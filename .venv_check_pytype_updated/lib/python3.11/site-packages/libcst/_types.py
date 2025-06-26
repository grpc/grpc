# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from pathlib import PurePath
from typing import TYPE_CHECKING, TypeVar, Union

if TYPE_CHECKING:
    from libcst._nodes.base import CSTNode  # noqa: F401


CSTNodeT = TypeVar("CSTNodeT", bound="CSTNode")
CSTNodeT_co = TypeVar("CSTNodeT_co", bound="CSTNode", covariant=True)
StrPath = Union[str, PurePath]
