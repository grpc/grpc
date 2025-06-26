# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# fmt: off
from typing import Sequence


class Item:
    def __init__(self, n: int) -> None:
        self.number: int = n


class ItemCollector:
    def get_items(self, n: int) -> Sequence[Item]:
        return [Item(i) for i in range(n)]


collector = ItemCollector()
items: Sequence[Item] = collector.get_items(3)
for item in items:
    item.number
