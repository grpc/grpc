# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
import argparse
from typing import Generator, Type

from libcst.codemod import Codemod, MagicArgsCodemodCommand
from libcst.codemod.visitors import AddImportsVisitor


class EnsureImportPresentCommand(MagicArgsCodemodCommand):
    DESCRIPTION: str = (
        "Given a module and possibly an entity in that module, add an import "
        + "as long as one does not already exist."
    )

    @staticmethod
    def add_args(arg_parser: argparse.ArgumentParser) -> None:
        arg_parser.add_argument(
            "--module",
            dest="module",
            metavar="MODULE",
            help="Module that should be imported.",
            type=str,
            required=True,
        )
        arg_parser.add_argument(
            "--entity",
            dest="entity",
            metavar="ENTITY",
            help=(
                "Entity that should be imported from module. If left empty, entire "
                + " module will be imported."
            ),
            type=str,
            default=None,
        )
        arg_parser.add_argument(
            "--alias",
            dest="alias",
            metavar="ALIAS",
            help=(
                "Alias that will be used for the imported module or entity. If left "
                + "empty, no alias will be applied."
            ),
            type=str,
            default=None,
        )

    def get_transforms(self) -> Generator[Type[Codemod], None, None]:
        AddImportsVisitor.add_needed_import(
            self.context,
            self.context.scratch["module"],
            self.context.scratch["entity"],
            self.context.scratch["alias"],
        )
        yield AddImportsVisitor
