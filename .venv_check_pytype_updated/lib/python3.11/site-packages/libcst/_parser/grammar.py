# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import re
from functools import lru_cache
from typing import FrozenSet, Iterator, Mapping, Optional, Tuple, Union

from libcst._parser.conversions.expression import (
    convert_arg_assign_comp_for,
    convert_arglist,
    convert_argument,
    convert_atom,
    convert_atom_basic,
    convert_atom_curlybraces,
    convert_atom_ellipses,
    convert_atom_expr,
    convert_atom_expr_await,
    convert_atom_expr_trailer,
    convert_atom_parens,
    convert_atom_squarebrackets,
    convert_atom_string,
    convert_binop,
    convert_boolop,
    convert_comp_for,
    convert_comp_if,
    convert_comp_op,
    convert_comparison,
    convert_dictorsetmaker,
    convert_expression_input,
    convert_factor,
    convert_fstring,
    convert_fstring_content,
    convert_fstring_conversion,
    convert_fstring_equality,
    convert_fstring_expr,
    convert_fstring_format_spec,
    convert_lambda,
    convert_namedexpr_test,
    convert_not_test,
    convert_power,
    convert_sliceop,
    convert_star_arg,
    convert_star_expr,
    convert_subscript,
    convert_subscriptlist,
    convert_sync_comp_for,
    convert_test,
    convert_test_nocond,
    convert_test_or_expr_list,
    convert_testlist_comp_list,
    convert_testlist_comp_tuple,
    convert_trailer,
    convert_trailer_arglist,
    convert_trailer_attribute,
    convert_trailer_subscriptlist,
    convert_yield_arg,
    convert_yield_expr,
)
from libcst._parser.conversions.module import convert_file_input
from libcst._parser.conversions.params import (
    convert_argslist,
    convert_fpdef,
    convert_fpdef_assign,
    convert_fpdef_slash,
    convert_fpdef_star,
    convert_fpdef_starstar,
)
from libcst._parser.conversions.statement import (
    convert_annassign,
    convert_assert_stmt,
    convert_assign,
    convert_asyncable_funcdef,
    convert_asyncable_stmt,
    convert_augassign,
    convert_break_stmt,
    convert_classdef,
    convert_compound_stmt,
    convert_continue_stmt,
    convert_decorated,
    convert_decorator,
    convert_decorators,
    convert_del_stmt,
    convert_dotted_as_name,
    convert_dotted_as_names,
    convert_dotted_name,
    convert_except_clause,
    convert_expr_stmt,
    convert_for_stmt,
    convert_funcdef,
    convert_funcdef_annotation,
    convert_global_stmt,
    convert_if_stmt,
    convert_if_stmt_elif,
    convert_if_stmt_else,
    convert_import_as_name,
    convert_import_as_names,
    convert_import_from,
    convert_import_name,
    convert_import_relative,
    convert_import_stmt,
    convert_indented_suite,
    convert_nonlocal_stmt,
    convert_parameters,
    convert_pass_stmt,
    convert_raise_stmt,
    convert_return_stmt,
    convert_simple_stmt_line,
    convert_simple_stmt_partial,
    convert_simple_stmt_suite,
    convert_small_stmt,
    convert_stmt,
    convert_stmt_input,
    convert_suite,
    convert_try_stmt,
    convert_while_stmt,
    convert_with_item,
    convert_with_stmt,
)
from libcst._parser.conversions.terminals import (
    convert_ASYNC,
    convert_AWAIT,
    convert_DEDENT,
    convert_ENDMARKER,
    convert_FSTRING_END,
    convert_FSTRING_START,
    convert_FSTRING_STRING,
    convert_INDENT,
    convert_NAME,
    convert_NEWLINE,
    convert_NUMBER,
    convert_OP,
    convert_STRING,
)
from libcst._parser.parso.pgen2.generator import generate_grammar, Grammar
from libcst._parser.parso.python.token import PythonTokenTypes, TokenType
from libcst._parser.parso.utils import parse_version_string, PythonVersionInfo
from libcst._parser.production_decorator import get_productions
from libcst._parser.types.config import AutoConfig
from libcst._parser.types.conversions import NonterminalConversion, TerminalConversion
from libcst._parser.types.production import Production

# Keep this sorted alphabetically
_TERMINAL_CONVERSIONS_SEQUENCE: Tuple[TerminalConversion, ...] = (
    convert_DEDENT,
    convert_ENDMARKER,
    convert_INDENT,
    convert_NAME,
    convert_NEWLINE,
    convert_NUMBER,
    convert_OP,
    convert_STRING,
    convert_FSTRING_START,
    convert_FSTRING_END,
    convert_FSTRING_STRING,
    convert_ASYNC,
    convert_AWAIT,
)

# Try to match the order of https://docs.python.org/3/reference/grammar.html
_NONTERMINAL_CONVERSIONS_SEQUENCE: Tuple[NonterminalConversion, ...] = (
    convert_file_input,
    convert_stmt_input,  # roughly equivalent to single_input
    convert_expression_input,  # roughly equivalent to eval_input
    convert_stmt,
    convert_simple_stmt_partial,
    convert_simple_stmt_line,
    convert_simple_stmt_suite,
    convert_small_stmt,
    convert_expr_stmt,
    convert_annassign,
    convert_augassign,
    convert_assign,
    convert_pass_stmt,
    convert_continue_stmt,
    convert_break_stmt,
    convert_del_stmt,
    convert_import_stmt,
    convert_import_name,
    convert_import_relative,
    convert_import_from,
    convert_import_as_name,
    convert_dotted_as_name,
    convert_import_as_names,
    convert_dotted_as_names,
    convert_dotted_name,
    convert_return_stmt,
    convert_raise_stmt,
    convert_global_stmt,
    convert_nonlocal_stmt,
    convert_assert_stmt,
    convert_compound_stmt,
    convert_if_stmt,
    convert_if_stmt_elif,
    convert_if_stmt_else,
    convert_while_stmt,
    convert_for_stmt,
    convert_try_stmt,
    convert_except_clause,
    convert_with_stmt,
    convert_with_item,
    convert_asyncable_funcdef,
    convert_funcdef,
    convert_classdef,
    convert_decorator,
    convert_decorators,
    convert_decorated,
    convert_asyncable_stmt,
    convert_parameters,
    convert_argslist,
    convert_fpdef_slash,
    convert_fpdef_star,
    convert_fpdef_starstar,
    convert_fpdef_assign,
    convert_fpdef,
    convert_funcdef_annotation,
    convert_suite,
    convert_indented_suite,
    convert_namedexpr_test,
    convert_test,
    convert_test_nocond,
    convert_lambda,
    convert_boolop,
    convert_not_test,
    convert_comparison,
    convert_comp_op,
    convert_star_expr,
    convert_binop,
    convert_factor,
    convert_power,
    convert_atom_expr,
    convert_atom_expr_await,
    convert_atom_expr_trailer,
    convert_trailer,
    convert_trailer_attribute,
    convert_trailer_subscriptlist,
    convert_subscriptlist,
    convert_subscript,
    convert_sliceop,
    convert_trailer_arglist,
    convert_atom,
    convert_atom_basic,
    convert_atom_parens,
    convert_atom_squarebrackets,
    convert_atom_curlybraces,
    convert_atom_string,
    convert_fstring,
    convert_fstring_content,
    convert_fstring_conversion,
    convert_fstring_equality,
    convert_fstring_expr,
    convert_fstring_format_spec,
    convert_atom_ellipses,
    convert_testlist_comp_tuple,
    convert_testlist_comp_list,
    convert_test_or_expr_list,
    convert_dictorsetmaker,
    convert_arglist,
    convert_argument,
    convert_arg_assign_comp_for,
    convert_star_arg,
    convert_sync_comp_for,
    convert_comp_for,
    convert_comp_if,
    convert_yield_expr,
    convert_yield_arg,
)


def get_grammar_str(version: PythonVersionInfo, future_imports: FrozenSet[str]) -> str:
    """
    Returns an BNF-like grammar text that `parso.pgen2.generator.generate_grammar` can
    handle.

    While you should generally use `get_grammar` instead, this can be useful for
    debugging the grammar.
    """
    lines = []
    for p in get_nonterminal_productions(version, future_imports):
        lines.append(str(p))
    return "\n".join(lines) + "\n"


# TODO: We should probably provide an on-disk cache like parso and lib2to3 do. Because
# of how we're defining our grammar, efficient cache invalidation is harder, though not
# impossible.
@lru_cache()
def get_grammar(
    version: PythonVersionInfo,
    future_imports: Union[FrozenSet[str], AutoConfig],
) -> "Grammar[TokenType]":
    if isinstance(future_imports, AutoConfig):
        # For easier testing, if not provided assume no __future__ imports
        future_imports = frozenset(())
    return generate_grammar(get_grammar_str(version, future_imports), PythonTokenTypes)


@lru_cache()
def get_terminal_conversions() -> Mapping[str, TerminalConversion]:
    """
    Returns a mapping from terminal type name to the conversion function that should be
    called by the parser.
    """
    return {
        # pyre-fixme[16]: Optional type has no attribute `group`.
        re.match("convert_(.*)", fn.__name__).group(1): fn
        for fn in _TERMINAL_CONVERSIONS_SEQUENCE
    }


@lru_cache()
def validate_grammar() -> None:
    for fn in _NONTERMINAL_CONVERSIONS_SEQUENCE:
        fn_productions = get_productions(fn)
        if all(p.name == fn_productions[0].name for p in fn_productions):
            # all the production names are the same, ensure that the `convert_` function
            # is named correctly
            production_name = fn_productions[0].name
            expected_name = f"convert_{production_name}"
            if fn.__name__ != expected_name:
                raise ValueError(
                    f"The conversion function for '{production_name}' "
                    + f"must be called '{expected_name}', not '{fn.__name__}'."
                )


def _get_version_comparison(version: str) -> Tuple[str, PythonVersionInfo]:
    if version[:2] in (">=", "<=", "==", "!="):
        return (version[:2], parse_version_string(version[2:].strip()))
    if version[:1] in (">", "<"):
        return (version[:1], parse_version_string(version[1:].strip()))
    raise ValueError(f"Invalid version comparison specifier '{version}'")


def _compare_versions(
    requested_version: PythonVersionInfo,
    actual_version: PythonVersionInfo,
    comparison: str,
) -> bool:
    if comparison == ">=":
        return actual_version >= requested_version
    if comparison == "<=":
        return actual_version <= requested_version
    if comparison == "==":
        return actual_version == requested_version
    if comparison == "!=":
        return actual_version != requested_version
    if comparison == ">":
        return actual_version > requested_version
    if comparison == "<":
        return actual_version < requested_version
    raise ValueError(f"Invalid version comparison specifier '{comparison}'")


def _should_include(
    requested_version: Optional[str], actual_version: PythonVersionInfo
) -> bool:
    if requested_version is None:
        return True
    for version in requested_version.split(","):
        comparison, parsed_version = _get_version_comparison(version.strip())
        if not _compare_versions(parsed_version, actual_version, comparison):
            return False
    return True


def _should_include_future(
    future: Optional[str],
    future_imports: FrozenSet[str],
) -> bool:
    if future is None:
        return True
    if future[:1] == "!":
        return future[1:] not in future_imports
    return future in future_imports


def get_nonterminal_productions(
    version: PythonVersionInfo, future_imports: FrozenSet[str]
) -> Iterator[Production]:
    for conversion in _NONTERMINAL_CONVERSIONS_SEQUENCE:
        for production in get_productions(conversion):
            if not _should_include(production.version, version):
                continue
            if not _should_include_future(production.future, future_imports):
                continue
            yield production


@lru_cache()
def get_nonterminal_conversions(
    version: PythonVersionInfo,
    future_imports: FrozenSet[str],
) -> Mapping[str, NonterminalConversion]:
    """
    Returns a mapping from nonterminal production name to the conversion function that
    should be called by the parser.
    """
    conversions = {}
    for fn in _NONTERMINAL_CONVERSIONS_SEQUENCE:
        for fn_production in get_productions(fn):
            if not _should_include(fn_production.version, version):
                continue
            if not _should_include_future(fn_production.future, future_imports):
                continue
            if fn_production.name in conversions:
                raise ValueError(
                    f"Found duplicate '{fn_production.name}' production in grammar"
                )
            conversions[fn_production.name] = fn

    return conversions
