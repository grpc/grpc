# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import cast, Collection, List, Set, Union

import libcst as cst
import libcst.matchers as m
from libcst.codemod._context import CodemodContext
from libcst.codemod._visitor import ContextAwareVisitor
from libcst.metadata import MetadataWrapper, QualifiedNameProvider

FUNCS_CONSIDERED_AS_STRING_ANNOTATIONS = {"typing.TypeVar"}


class GatherNamesFromStringAnnotationsVisitor(ContextAwareVisitor):
    """
    Collects all names from string literals used for typing purposes.
    This includes annotations like ``foo: "SomeType"``, and parameters to
    special functions related to typing (currently only `typing.TypeVar`).

    After visiting, a set of all found names will be available on the ``names``
    attribute of this visitor.
    """

    METADATA_DEPENDENCIES = (QualifiedNameProvider,)

    def __init__(
        self,
        context: CodemodContext,
        typing_functions: Collection[str] = FUNCS_CONSIDERED_AS_STRING_ANNOTATIONS,
    ) -> None:
        super().__init__(context)
        self._typing_functions: Collection[str] = typing_functions
        self._annotation_stack: List[cst.CSTNode] = []
        #: The set of names collected from string literals.
        self.names: Set[str] = set()

    def visit_Annotation(self, node: cst.Annotation) -> bool:
        self._annotation_stack.append(node)
        return True

    def leave_Annotation(self, original_node: cst.Annotation) -> None:
        self._annotation_stack.pop()

    def visit_Subscript(self, node: cst.Subscript) -> bool:
        qnames = self.get_metadata(QualifiedNameProvider, node)
        # A Literal["foo"] should not be interpreted as a use of the symbol "foo".
        return not any(qn.name == "typing.Literal" for qn in qnames)

    def visit_Call(self, node: cst.Call) -> bool:
        qnames = self.get_metadata(QualifiedNameProvider, node)
        if any(qn.name in self._typing_functions for qn in qnames):
            self._annotation_stack.append(node)
            return True
        return False

    def leave_Call(self, original_node: cst.Call) -> None:
        if self._annotation_stack and self._annotation_stack[-1] == original_node:
            self._annotation_stack.pop()

    def visit_ConcatenatedString(self, node: cst.ConcatenatedString) -> bool:
        if self._annotation_stack:
            self.handle_any_string(node)
        return False

    def visit_SimpleString(self, node: cst.SimpleString) -> bool:
        if self._annotation_stack:
            self.handle_any_string(node)
        return False

    def handle_any_string(
        self, node: Union[cst.SimpleString, cst.ConcatenatedString]
    ) -> None:
        value = node.evaluated_value
        if value is None:
            return
        try:
            mod = cst.parse_module(value)
        except cst.ParserSyntaxError:
            # Not all strings inside a type annotation are meant to be valid Python code.
            return
        extracted_nodes = m.extractall(
            mod,
            m.Name(
                value=m.SaveMatchedNode(m.DoNotCare(), "name"),
                metadata=m.MatchMetadataIfTrue(
                    cst.metadata.ParentNodeProvider,
                    lambda parent: not isinstance(parent, cst.Attribute),
                ),
            )
            | m.SaveMatchedNode(m.Attribute(), "attribute"),
            metadata_resolver=MetadataWrapper(mod, unsafe_skip_copy=True),
        )
        names = {
            cast(str, values["name"]) for values in extracted_nodes if "name" in values
        } | {
            name
            for values in extracted_nodes
            if "attribute" in values
            for name, _ in cst.metadata.scope_provider._gen_dotted_names(
                cast(cst.Attribute, values["attribute"])
            )
        }
        self.names.update(names)
