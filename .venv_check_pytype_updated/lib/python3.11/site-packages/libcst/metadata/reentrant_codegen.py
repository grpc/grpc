# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


from dataclasses import dataclass, field
from typing import List, Optional, Sequence

from libcst import BaseStatement, CSTNode, Module
from libcst._add_slots import add_slots
from libcst._nodes.internal import CodegenState
from libcst.metadata import BaseMetadataProvider


class CodegenPartial:
    """
    Provided by :class:`ExperimentalReentrantCodegenProvider`.

    Stores enough information to generate either a small patch
    (:meth:`get_modified_code_range`) or a new file (:meth:`get_modified_code`) by
    replacing the old node at this position.
    """

    __slots__ = [
        "start_offset",
        "end_offset",
        "has_trailing_newline",
        "_indent_tokens",
        "_prev_codegen_state",
    ]

    def __init__(self, state: "_ReentrantCodegenState") -> None:
        # store a frozen copy of these values, since they change over time
        self.start_offset: int = state.start_offset_stack[-1]
        self.end_offset: int = state.char_offset
        self.has_trailing_newline: bool = True  # this may get updated to False later
        self._indent_tokens: Sequence[str] = tuple(state.indent_tokens)
        # everything else can be accessed from the codegen state object
        self._prev_codegen_state: _ReentrantCodegenState = state

    def get_original_module_code(self) -> str:
        """
        Equivalent to :meth:`libcst.Module.bytes` on the top-level module that contains
        this statement, except that it uses the cached result from our previous code
        generation pass, so it's faster.
        """
        return self._prev_codegen_state.get_code()

    def get_original_module_bytes(self) -> bytes:
        """
        Equivalent to :meth:`libcst.Module.bytes` on the top-level module that contains
        this statement, except that it uses the cached result from our previous code
        generation pass, so it's faster.
        """
        return self.get_original_module_code().encode(self._prev_codegen_state.encoding)

    def get_original_statement_code(self) -> str:
        """
        Equivalent to :meth:`libcst.Module.code_for_node` on the current statement,
        except that it uses the cached result from our previous code generation pass,
        so it's faster.
        """
        return self._prev_codegen_state.get_code()[self.start_offset : self.end_offset]

    def get_modified_statement_code(self, node: BaseStatement) -> str:
        """
        Gets the new code for ``node`` as if it were in same location as the old
        statement being replaced. This means that it inherits details like the old
        statement's indentation.
        """
        new_codegen_state = CodegenState(
            default_indent=self._prev_codegen_state.default_indent,
            default_newline=self._prev_codegen_state.default_newline,
            indent_tokens=list(self._indent_tokens),
        )
        node._codegen(new_codegen_state)
        if not self.has_trailing_newline:
            new_codegen_state.pop_trailing_newline()
        return "".join(new_codegen_state.tokens)

    def get_modified_module_code(self, node: BaseStatement) -> str:
        """
        Gets the new code for the module at the root of this statement's tree, but with
        the supplied replacement ``node`` in its place.
        """
        original = self.get_original_module_code()
        patch = self.get_modified_statement_code(node)
        return f"{original[:self.start_offset]}{patch}{original[self.end_offset:]}"

    def get_modified_module_bytes(self, node: BaseStatement) -> bytes:
        """
        Gets the new bytes for the module at the root of this statement's tree, but with
        the supplied replacement ``node`` in its place.
        """
        return self.get_modified_module_code(node).encode(
            self._prev_codegen_state.encoding
        )


@add_slots
@dataclass(frozen=False)
class _ReentrantCodegenState(CodegenState):
    provider: BaseMetadataProvider[CodegenPartial]
    encoding: str = "utf-8"
    indent_size: int = 0
    char_offset: int = 0
    start_offset_stack: List[int] = field(default_factory=list)
    cached_code: Optional[str] = None
    trailing_partials: List[CodegenPartial] = field(default_factory=list)

    def increase_indent(self, value: str) -> None:
        super(_ReentrantCodegenState, self).increase_indent(value)
        self.indent_size += len(value)

    def decrease_indent(self) -> None:
        self.indent_size -= len(self.indent_tokens[-1])
        super(_ReentrantCodegenState, self).decrease_indent()

    def add_indent_tokens(self) -> None:
        super(_ReentrantCodegenState, self).add_indent_tokens()
        self.char_offset += self.indent_size

    def add_token(self, value: str) -> None:
        super(_ReentrantCodegenState, self).add_token(value)
        self.char_offset += len(value)
        self.trailing_partials.clear()

    def before_codegen(self, node: CSTNode) -> None:
        if not isinstance(node, BaseStatement):
            return

        self.start_offset_stack.append(self.char_offset)

    def after_codegen(self, node: CSTNode) -> None:
        if not isinstance(node, BaseStatement):
            return

        partial = CodegenPartial(self)
        self.provider.set_metadata(node, partial)
        self.start_offset_stack.pop()
        self.trailing_partials.append(partial)

    def pop_trailing_newline(self) -> None:
        """
        :class:`libcst.Module` contains a hack where it removes the last token (a
        newline) if the original file didn't have a newline.

        If this happens, we need to go back through every node at the end of the file,
        and fix their `end_offset`.
        """
        for tp in self.trailing_partials:
            tp.end_offset -= len(self.tokens[-1])
            tp.has_trailing_newline = False
        super(_ReentrantCodegenState, self).pop_trailing_newline()

    def get_code(self) -> str:
        # Ideally this would use functools.cached_property, but that's only in
        # Python 3.8+.
        #
        # This is a little ugly to make pyre's attribute refinement checks happy.
        cached_code = self.cached_code
        if cached_code is not None:
            return cached_code
        cached_code = "".join(self.tokens)
        self.cached_code = cached_code
        return cached_code


class ExperimentalReentrantCodegenProvider(BaseMetadataProvider[CodegenPartial]):
    """
    An experimental API that allows fast generation of modified code by recording an
    initial code-generation pass, and incrementally applying updates. It is a
    performance optimization for a few niche use-cases and is not user-friendly.

    **This API may change at any time without warning (including in minor releases).**

    This is rarely useful. Instead you should make multiple modifications to a single
    syntax tree, and generate the code once. However, we can think of a few use-cases
    for this API (hence, why it exists):

    - When linting a file, you might generate multiple independent patches that a user
      can accept or reject. Depending on your architecture, it may be advantageous to
      avoid regenerating the file when computing each patch.

    - You might want to call out to an external utility (e.g. a typechecker, such as
      pyre or mypy) to validate a small change. You may need to generate and test lots
      of these patches.

    Restrictions:

    - For safety and sanity reasons, the smallest/only level of granularity is a
      statement. If you need to patch part of a statement, you regenerate the entire
      statement. If you need to regenerate an entire module, just call
      :meth:`libcst.Module.code`.

    - This does not (currently) operate recursively. You can patch an unpatched piece
      of code multiple times, but you can't layer additional patches on an already
      patched piece of code.
    """

    def _gen_impl(self, module: Module) -> None:
        state = _ReentrantCodegenState(
            default_indent=module.default_indent,
            default_newline=module.default_newline,
            provider=self,
            encoding=module.encoding,
        )
        module._codegen(state)
