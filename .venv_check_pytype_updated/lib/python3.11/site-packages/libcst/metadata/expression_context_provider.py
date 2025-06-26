# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from enum import auto, Enum
from typing import Optional, Sequence

import libcst as cst
from libcst.metadata.base_provider import BatchableMetadataProvider


class ExpressionContext(Enum):
    """Used in :class:`ExpressionContextProvider` to represent context of a variable
    reference."""

    #: Load the value of a variable reference.
    #:
    #: >>> libcst.MetadataWrapper(libcst.parse_module("a")).resolve(libcst.ExpressionContextProvider)
    #: mappingproxy({Name(
    #:                   value='a',
    #:                   lpar=[],
    #:                   rpar=[],
    #:               ): <ExpressionContext.LOAD: 1>})
    LOAD = auto()

    #: Store a value to a variable reference by :class:`~libcst.Assign` (``=``),
    #: :class:`~libcst.AugAssign` (e.g. ``+=``, ``-=``, etc), or
    #: :class:`~libcst.AnnAssign`.
    #:
    #: >>> libcst.MetadataWrapper(libcst.parse_module("a = b")).resolve(libcst.ExpressionContextProvider)
    #: mappingproxy({Name(
    #:               value='a',
    #:               lpar=[],
    #:               rpar=[],
    #:           ): <ExpressionContext.STORE: 2>, Name(
    #:               value='b',
    #:               lpar=[],
    #:               rpar=[],
    #:           ): <ExpressionContext.LOAD: 1>})
    STORE = auto()

    #: Delete value of a variable reference by ``del``.
    #:
    #: >>> libcst.MetadataWrapper(libcst.parse_module("del a")).resolve(libcst.ExpressionContextProvider)
    #: mappingproxy({Name(
    #:                   value='a',
    #:                   lpar=[],
    #:                   rpar=[],
    #:               ): < ExpressionContext.DEL: 3 >})
    DEL = auto()


class ExpressionContextVisitor(cst.CSTVisitor):
    def __init__(
        self, provider: "ExpressionContextProvider", context: ExpressionContext
    ) -> None:
        self.provider = provider
        self.context = context

    def visit_Assign(self, node: cst.Assign) -> bool:
        for target in node.targets:
            target.visit(
                ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
            )
        node.value.visit(self)
        return False

    def visit_AnnAssign(self, node: cst.AnnAssign) -> bool:
        node.target.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
        )
        node.annotation.visit(self)
        value = node.value
        if value:
            value.visit(self)
        return False

    def visit_AugAssign(self, node: cst.AugAssign) -> bool:
        node.target.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
        )
        node.value.visit(self)
        return False

    def visit_NamedExpr(self, node: cst.NamedExpr) -> bool:
        node.target.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
        )
        node.value.visit(self)
        return False

    def visit_Name(self, node: cst.Name) -> bool:
        self.provider.set_metadata(node, self.context)
        return False

    def visit_AsName(self, node: cst.AsName) -> Optional[bool]:
        node.name.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
        )
        return False

    def visit_CompFor(self, node: cst.CompFor) -> bool:
        node.target.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
        )
        node.iter.visit(self)
        for i in node.ifs:
            i.visit(self)
        inner_for_in = node.inner_for_in
        if inner_for_in:
            inner_for_in.visit(self)
        return False

    def visit_For(self, node: cst.For) -> bool:
        node.target.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
        )
        node.iter.visit(self)
        node.body.visit(self)
        orelse = node.orelse
        if orelse:
            orelse.visit(self)
        return False

    def visit_Del(self, node: cst.Del) -> bool:
        node.target.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.DEL)
        )
        return False

    def visit_Attribute(self, node: cst.Attribute) -> bool:
        self.provider.set_metadata(node, self.context)
        node.value.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.LOAD)
        )
        # don't visit attr (Name), so attr has no context
        return False

    def visit_Subscript(self, node: cst.Subscript) -> bool:
        self.provider.set_metadata(node, self.context)
        node.value.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.LOAD)
        )
        slice = node.slice
        if isinstance(slice, Sequence):
            for sli in slice:
                sli.visit(
                    ExpressionContextVisitor(self.provider, ExpressionContext.LOAD)
                )
        else:
            slice.visit(ExpressionContextVisitor(self.provider, ExpressionContext.LOAD))
        return False

    def visit_Tuple(self, node: cst.Tuple) -> Optional[bool]:
        self.provider.set_metadata(node, self.context)

    def visit_List(self, node: cst.List) -> Optional[bool]:
        self.provider.set_metadata(node, self.context)

    def visit_StarredElement(self, node: cst.StarredElement) -> Optional[bool]:
        self.provider.set_metadata(node, self.context)

    def visit_ClassDef(self, node: cst.ClassDef) -> Optional[bool]:
        node.name.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
        )
        node.body.visit(self)
        for base in node.bases:
            base.visit(self)
        for keyword in node.keywords:
            keyword.visit(self)
        for decorator in node.decorators:
            decorator.visit(self)
        return False

    def visit_FunctionDef(self, node: cst.FunctionDef) -> Optional[bool]:
        node.name.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
        )
        node.params.visit(self)
        node.body.visit(self)
        for decorator in node.decorators:
            decorator.visit(self)
        returns = node.returns
        if returns:
            returns.visit(self)
        return False

    def visit_Param(self, node: cst.Param) -> Optional[bool]:
        node.name.visit(
            ExpressionContextVisitor(self.provider, ExpressionContext.STORE)
        )
        annotation = node.annotation
        if annotation:
            annotation.visit(self)
        default = node.default
        if default:
            default.visit(self)
        return False


class ExpressionContextProvider(BatchableMetadataProvider[ExpressionContext]):
    """
    Provides :class:`ExpressionContext` metadata (mimics the `expr_context
    <https://docs.python.org/3/library/ast.html>`__ in ast) for the
    following node types:
    :class:`~libcst.Attribute`, :class:`~libcst.Subscript`,
    :class:`~libcst.StarredElement` , :class:`~libcst.List`,
    :class:`~libcst.Tuple` and :class:`~libcst.Name`.
    Note that a :class:`~libcst.Name` may not always have context because of the differences between
    ast and LibCST. E.g. :attr:`~libcst.Attribute.attr` is a :class:`~libcst.Name` in LibCST
    but a str in ast. To honor ast implementation, we don't assign context to
    :attr:`~libcst.Attribute.attr`.


    Three context types :attr:`ExpressionContext.STORE`,
    :attr:`ExpressionContext.LOAD` and :attr:`ExpressionContext.DEL` are provided.
    """

    def visit_Module(self, node: cst.Module) -> Optional[bool]:
        node.visit(ExpressionContextVisitor(self, ExpressionContext.LOAD))
