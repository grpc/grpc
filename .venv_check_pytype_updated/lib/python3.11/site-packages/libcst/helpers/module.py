# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from dataclasses import dataclass
from itertools import islice
from pathlib import Path, PurePath
from typing import List, Optional

from libcst import Comment, EmptyLine, ImportFrom, Module
from libcst._types import StrPath
from libcst.helpers.expression import get_full_name_for_node


def insert_header_comments(node: Module, comments: List[str]) -> Module:
    """
    Insert comments after last non-empty line in header. Use this to insert one or more
    comments after any copyright preamble in a :class:`~libcst.Module`. Each comment in
    the list of ``comments`` must start with a ``#`` and will be placed on its own line
    in the appropriate location.
    """
    # Split the lines up into a contiguous comment-containing section and
    # the empty whitespace section that follows
    last_comment_index = -1
    for i, line in enumerate(node.header):
        if line.comment is not None:
            last_comment_index = i

    comment_lines = islice(node.header, last_comment_index + 1)
    empty_lines = islice(node.header, last_comment_index + 1, None)
    inserted_lines = [EmptyLine(comment=Comment(value=comment)) for comment in comments]
    # pyre-fixme[60]: Concatenation not yet support for multiple variadic tuples:
    #  `*comment_lines, *inserted_lines, *empty_lines`.
    return node.with_changes(header=(*comment_lines, *inserted_lines, *empty_lines))


def get_absolute_module(
    current_module: Optional[str], module_name: Optional[str], num_dots: int
) -> Optional[str]:
    if num_dots == 0:
        # This is an absolute import, so the module is correct.
        return module_name
    if current_module is None:
        # We don't actually have the current module available, so we can't compute
        # the absolute module from relative.
        return None
    # We have the current module, as well as the relative, let's compute the base.
    modules = current_module.split(".")
    if len(modules) < num_dots:
        # This relative import goes past the base of the repository, so we can't calculate it.
        return None
    base_module = ".".join(modules[:-num_dots])
    # Finally, if the module name was supplied, append it to the end.
    if module_name is not None:
        # If we went all the way to the top, the base module should be empty, so we
        # should return the relative bit as absolute. Otherwise, combine the base
        # module and module name using a dot separator.
        base_module = (
            f"{base_module}.{module_name}" if len(base_module) > 0 else module_name
        )
    # If they tried to import all the way to the root, return None. Otherwise,
    # return the module itself.
    return base_module if len(base_module) > 0 else None


def get_absolute_module_for_import(
    current_module: Optional[str], import_node: ImportFrom
) -> Optional[str]:
    # First, let's try to grab the module name, regardless of relative status.
    module = import_node.module
    module_name = get_full_name_for_node(module) if module is not None else None
    # Now, get the relative import location if it exists.
    num_dots = len(import_node.relative)
    return get_absolute_module(current_module, module_name, num_dots)


def get_absolute_module_for_import_or_raise(
    current_module: Optional[str], import_node: ImportFrom
) -> str:
    module = get_absolute_module_for_import(current_module, import_node)
    if module is None:
        raise ValueError(f"Unable to compute absolute module for {import_node}")
    return module


def get_absolute_module_from_package(
    current_package: Optional[str], module_name: Optional[str], num_dots: int
) -> Optional[str]:
    if num_dots == 0:
        # This is an absolute import, so the module is correct.
        return module_name
    if current_package is None or current_package == "":
        # We don't actually have the current module available, so we can't compute
        # the absolute module from relative.
        return None

    # see importlib._bootstrap._resolve_name
    # https://github.com/python/cpython/blob/3.10/Lib/importlib/_bootstrap.py#L902
    bits = current_package.rsplit(".", num_dots - 1)
    if len(bits) < num_dots:
        return None

    base = bits[0]
    return "{}.{}".format(base, module_name) if module_name else base


def get_absolute_module_from_package_for_import(
    current_package: Optional[str], import_node: ImportFrom
) -> Optional[str]:
    # First, let's try to grab the module name, regardless of relative status.
    module = import_node.module
    module_name = get_full_name_for_node(module) if module is not None else None
    # Now, get the relative import location if it exists.
    num_dots = len(import_node.relative)
    return get_absolute_module_from_package(current_package, module_name, num_dots)


def get_absolute_module_from_package_for_import_or_raise(
    current_package: Optional[str], import_node: ImportFrom
) -> str:
    module = get_absolute_module_from_package_for_import(current_package, import_node)
    if module is None:
        raise ValueError(f"Unable to compute absolute module for {import_node}")
    return module


@dataclass(frozen=True)
class ModuleNameAndPackage:
    name: str
    package: str


def calculate_module_and_package(
    repo_root: StrPath, filename: StrPath, use_pyproject_toml: bool = False
) -> ModuleNameAndPackage:
    # Given an absolute repo_root and an absolute filename, calculate the
    # python module name for the file.
    if use_pyproject_toml:
        # But also look for pyproject.toml files, indicating nested packages in the repo.
        abs_repo_root = Path(repo_root).resolve()
        abs_filename = Path(filename).resolve()
        package_root = abs_filename.parent
        while package_root != abs_repo_root:
            if (package_root / "pyproject.toml").exists():
                break
            if package_root == package_root.parent:
                break
            package_root = package_root.parent

        relative_filename = abs_filename.relative_to(package_root)
    else:
        relative_filename = PurePath(filename).relative_to(repo_root)
    relative_filename = relative_filename.with_suffix("")

    # handle special cases
    if relative_filename.stem in ["__init__", "__main__"]:
        relative_filename = relative_filename.parent
        package = name = ".".join(relative_filename.parts)
    else:
        name = ".".join(relative_filename.parts)
        package = ".".join(relative_filename.parts[:-1])

    return ModuleNameAndPackage(name, package)
