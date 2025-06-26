# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import pickle
from dataclasses import dataclass
from typing import ClassVar

from libcst._add_slots import add_slots

from libcst.testing.utils import UnitTest


# this test class needs to be defined at module level to test pickling.
@add_slots
@dataclass(frozen=True)
class A:
    x: int
    y: str

    Z: ClassVar[int] = 5


class AddSlotsTest(UnitTest):
    def test_pickle(self) -> None:
        a = A(1, "foo")
        self.assertEqual(a, pickle.loads(pickle.dumps(a)))
        object.__delattr__(a, "y")
        self.assertEqual(a.x, pickle.loads(pickle.dumps(a)).x)

    def test_prevents_slots_overlap(self) -> None:
        class A:
            __slots__ = ("x",)

        class B(A):
            __slots__ = ("z",)

        @add_slots
        @dataclass
        class C(B):
            x: int
            y: str
            z: bool

        self.assertSequenceEqual(C.__slots__, ("y",))
