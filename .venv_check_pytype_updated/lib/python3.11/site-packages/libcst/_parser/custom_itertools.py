# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from itertools import zip_longest
from typing import Iterable, Iterator, TypeVar

_T = TypeVar("_T")


# https://docs.python.org/3/library/itertools.html#itertools-recipes
def grouper(iterable: Iterable[_T], n: int, fillvalue: _T = None) -> Iterator[_T]:
    "Collect data into fixed-length chunks or blocks"
    # grouper('ABCDEFG', 3, 'x') --> ABC DEF Gxx"
    args = [iter(iterable)] * n
    return zip_longest(*args, fillvalue=fillvalue)
