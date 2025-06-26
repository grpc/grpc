# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from libcst import Module
from libcst.codemod import CodemodCommand


class NOOPCommand(CodemodCommand):
    DESCRIPTION: str = "Does absolutely nothing."

    def transform_module_impl(self, tree: Module) -> Module:
        # Return the tree as-is, with absolutely no modification
        return tree
