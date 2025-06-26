# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# pyre-strict
from functools import partial
from typing import cast, Generator

from libcst.codemod import Codemod, MagicArgsCodemodCommand
from libcst.codemod.commands.rename import RenameCommand


class RenameTypingGenericAliases(MagicArgsCodemodCommand):
    DESCRIPTION: str = (
        "Rename typing module aliases of builtin generics in Python 3.9+, for example: `typing.List` -> `list`"
    )

    MAPPING: dict[str, str] = {
        "typing.List": "builtins.list",
        "typing.Tuple": "builtins.tuple",
        "typing.Dict": "builtins.dict",
        "typing.FrozenSet": "builtins.frozenset",
        "typing.Set": "builtins.set",
        "typing.Type": "builtins.type",
    }

    def get_transforms(self) -> Generator[type[Codemod], None, None]:
        for from_type, to_type in self.MAPPING.items():
            yield cast(
                type[Codemod],
                partial(
                    RenameCommand,
                    old_name=from_type,
                    new_name=to_type,
                ),
            )
