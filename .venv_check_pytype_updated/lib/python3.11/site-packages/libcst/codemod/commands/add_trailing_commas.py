# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import argparse
import textwrap
from typing import Dict, Optional

import libcst as cst
from libcst.codemod import CodemodContext, VisitorBasedCodemodCommand


presets_per_formatter: Dict[str, Dict[str, int]] = {
    "black": {
        "parameter_count": 1,
        "argument_count": 2,
    },
    "yapf": {
        "parameter_count": 2,
        "argument_count": 2,
    },
}


class AddTrailingCommas(VisitorBasedCodemodCommand):
    DESCRIPTION: str = textwrap.dedent(
        """
        Codemod that adds trailing commas to arguments in function
        headers and function calls.

        The idea is that both the black and yapf autoformatters will
        tend to split headers and function calls so that there
        is one parameter / argument per line if there is a trailing
        comma:
        - Black will always separate them by line
        - Yapf appears to do so whenever there are at least two arguments

        Applying this codemod (and then an autoformatter) may make
        it easier to read function definitions and calls
        """
    )

    def __init__(
        self,
        context: CodemodContext,
        formatter: str = "black",
        parameter_count: Optional[int] = None,
        argument_count: Optional[int] = None,
    ) -> None:
        super().__init__(context)
        presets = presets_per_formatter.get(formatter)
        if presets is None:
            raise ValueError(
                f"Unknown formatter {formatter!r}. Presets exist for "
                + ", ".join(presets_per_formatter.keys())
            )
        self.parameter_count: int = parameter_count or presets["parameter_count"]
        self.argument_count: int = argument_count or presets["argument_count"]

    @staticmethod
    def add_args(arg_parser: argparse.ArgumentParser) -> None:
        arg_parser.add_argument(
            "--formatter",
            dest="formatter",
            metavar="FORMATTER",
            help="Formatter to target (e.g. yapf or black)",
            type=str,
            default="black",
        )
        arg_parser.add_argument(
            "--paramter-count",
            dest="parameter_count",
            metavar="PARAMETER_COUNT",
            help="Minimal number of parameters for us to add trailing comma",
            type=int,
            default=None,
        )
        arg_parser.add_argument(
            "--argument-count",
            dest="argument_count",
            metavar="ARGUMENT_COUNT",
            help="Minimal number of arguments for us to add trailing comma",
            type=int,
            default=None,
        )

    def leave_Parameters(
        self,
        original_node: cst.Parameters,
        updated_node: cst.Parameters,
    ) -> cst.Parameters:
        skip = (
            #
            self.parameter_count is None
            or len(updated_node.params) < self.parameter_count
            or (
                len(updated_node.params) == 1
                and updated_node.params[0].name.value in {"self", "cls"}
            )
        )
        if skip:
            return updated_node
        else:
            last_param = updated_node.params[-1]
            return updated_node.with_changes(
                params=(
                    *updated_node.params[:-1],
                    last_param.with_changes(comma=cst.Comma()),
                ),
            )

    def leave_Call(
        self,
        original_node: cst.Call,
        updated_node: cst.Call,
    ) -> cst.Call:
        if len(updated_node.args) < self.argument_count:
            return updated_node
        else:
            last_arg = updated_node.args[-1]
            return updated_node.with_changes(
                args=(
                    *updated_node.args[:-1],
                    last_arg.with_changes(comma=cst.Comma()),
                ),
            )
