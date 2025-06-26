# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from dataclasses import dataclass
from typing import cast, Optional, Sequence, TYPE_CHECKING, TypeVar, Union

from libcst._add_slots import add_slots
from libcst._nodes.base import CSTNode
from libcst._nodes.internal import CodegenState, visit_body_sequence, visit_sequence
from libcst._nodes.statement import (
    BaseCompoundStatement,
    get_docstring_impl,
    SimpleStatementLine,
)
from libcst._nodes.whitespace import EmptyLine
from libcst._removal_sentinel import RemovalSentinel
from libcst._visitors import CSTVisitorT

if TYPE_CHECKING:
    # This is circular, so import the type only in type checking
    from libcst._parser.types.config import PartialParserConfig


_ModuleSelfT = TypeVar("_ModuleSelfT", bound="Module")

# type alias needed for scope overlap in type definition
builtin_bytes = bytes


@add_slots
@dataclass(frozen=True)
class Module(CSTNode):
    """
    Contains some top-level information inferred from the file letting us set correct
    defaults when printing the tree about global formatting rules. All code parsed
    with :func:`parse_module` will be encapsulated in a module.
    """

    #: A list of zero or more statements that make up this module.
    body: Sequence[Union[SimpleStatementLine, BaseCompoundStatement]]

    #: Normally any whitespace/comments are assigned to the next node visited, but
    #: :class:`Module` is a special case, and comments at the top of the file tend
    #: to refer to the module itself, so we assign them to the :class:`Module`
    #: instead of the first statement in the body.
    header: Sequence[EmptyLine] = ()

    #: Any trailing whitespace/comments found after the last statement.
    footer: Sequence[EmptyLine] = ()

    #: The file's encoding format. When parsing a ``bytes`` object, this value may be
    #: inferred from the contents of the parsed source code. When parsing a ``str``,
    #: this value defaults to ``"utf-8"``.
    #:
    #: This value affects how :attr:`bytes` encodes the source code.
    encoding: str = "utf-8"

    #: The indentation of the file, expressed as a series of tabs and/or spaces. This
    #: value is inferred from the contents of the parsed source code by default.
    default_indent: str = " " * 4

    #: The newline of the file, expressed as ``\n``, ``\r\n``, or ``\r``. This value is
    #: inferred from the contents of the parsed source code by default.
    default_newline: str = "\n"

    #: Whether the module has a trailing newline or not.
    has_trailing_newline: bool = True

    def _visit_and_replace_children(self, visitor: CSTVisitorT) -> "Module":
        return Module(
            header=visit_sequence(self, "header", self.header, visitor),
            body=visit_body_sequence(self, "body", self.body, visitor),
            footer=visit_sequence(self, "footer", self.footer, visitor),
            encoding=self.encoding,
            default_indent=self.default_indent,
            default_newline=self.default_newline,
            has_trailing_newline=self.has_trailing_newline,
        )

    def visit(self: _ModuleSelfT, visitor: CSTVisitorT) -> _ModuleSelfT:
        """
        Returns the result of running a visitor over this module.

        :class:`Module` overrides the default visitor entry point to resolve metadata
        dependencies declared by 'visitor'.
        """
        result = super(Module, self).visit(visitor)
        if isinstance(result, RemovalSentinel):
            return self.with_changes(body=(), header=(), footer=())
        else:  # is a Module
            return cast(_ModuleSelfT, result)

    def _codegen_impl(self, state: CodegenState) -> None:
        for h in self.header:
            h._codegen(state)
        for stmt in self.body:
            stmt._codegen(state)
        for f in self.footer:
            f._codegen(state)
        if self.has_trailing_newline:
            if len(state.tokens) == 0:
                # There was nothing in the header, footer, or body. Just add a newline
                # to preserve the trailing newline.
                state.add_token(state.default_newline)
        else:  # has_trailing_newline is false
            state.pop_trailing_newline()

    @property
    def code(self) -> str:
        """
        The string representation of this module, respecting the inferred indentation
        and newline type.
        """
        return self.code_for_node(self)

    @property
    def bytes(self) -> builtin_bytes:
        """
        The bytes representation of this module, respecting the inferred indentation
        and newline type, using the current encoding.
        """
        return self.code.encode(self.encoding)

    def code_for_node(self, node: CSTNode) -> str:
        """
        Generates the code for the given node in the context of this module. This is a
        method of Module, not CSTNode, because we need to know the module's default
        indentation and newline formats.
        """

        state = CodegenState(
            default_indent=self.default_indent, default_newline=self.default_newline
        )
        node._codegen(state)
        return "".join(state.tokens)

    @property
    def config_for_parsing(self) -> "PartialParserConfig":
        """
        Generates a parser config appropriate for passing to a :func:`parse_expression`
        or :func:`parse_statement` call. This is useful when using either parser
        function to generate code from a string template. By using a generated parser
        config instead of the default, you can guarantee that trees generated from
        both statement and expression strings have the same inferred defaults for things
        like newlines, indents and similar::

            module = cst.parse_module("pass\\n")
            expression = cst.parse_expression("1 + 2", config=module.config_for_parsing)
        """

        from libcst._parser.types.config import PartialParserConfig

        return PartialParserConfig(
            encoding=self.encoding,
            default_indent=self.default_indent,
            default_newline=self.default_newline,
        )

    def get_docstring(self, clean: bool = True) -> Optional[str]:
        """
        Returns a :func:`inspect.cleandoc` cleaned docstring if the docstring is available, ``None`` otherwise.
        """
        return get_docstring_impl(self.body, clean)
