# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from typing import Optional

import libcst as cst
from libcst.metadata.base_provider import BatchableMetadataProvider


class ParentNodeVisitor(cst.CSTVisitor):
    def __init__(self, provider: "ParentNodeProvider") -> None:
        self.provider: ParentNodeProvider = provider
        super().__init__()

    def on_leave(self, original_node: cst.CSTNode) -> None:
        for child in original_node.children:
            self.provider.set_metadata(child, original_node)
        super().on_leave(original_node)


class ParentNodeProvider(BatchableMetadataProvider[cst.CSTNode]):
    def visit_Module(self, node: cst.Module) -> Optional[bool]:
        node.visit(ParentNodeVisitor(self))
