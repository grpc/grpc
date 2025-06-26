"""Merges type annotations from pyi files into the corresponding py files."""

import difflib
import enum
import os
import re
import shutil

import libcst as cst
from libcst import codemod
from libcst._nodes import expression
from libcst.codemod import visitors
from pytype.imports import pickle_utils
from pytype.platform_utils import path_utils
from pytype.pytd import pytd_utils


class MergeError(Exception):
  """Wrap exceptions thrown while merging files."""


def _merge_csts(*, py_tree, pyi_tree):
  context = codemod.CodemodContext()
  vis = visitors.ApplyTypeAnnotationsVisitor
  vis.store_stub_in_context(context, pyi_tree)
  return vis(
      context,
      overwrite_existing_annotations=False,
      strict_posargs_matching=False,
      strict_annotation_matching=True,
  ).transform_module(py_tree)


class RemoveAnyNeverTransformer(cst.CSTTransformer):
  """Transform away every `Any` and `Never` annotations in function returns and variable assignments.

  For putting 'Any's, it's basically a no-op, and it doesn't help readability
  so better not put anything when pytype gives up.

  Having 'Never' annotated on function returns and variables is valid, but
  they're most likely wrong if it's inferred by pytype, and it has a chain
  effect that all downstream code starts to get treated as unreachable.
  """

  def _is_any_or_never(self, annotation: expression.Annotation | None):
    return (
        annotation
        and isinstance(annotation, expression.Name)
        and annotation.value in ("Any", "Never")
    )

  def leave_FunctionDef(
      self, original_node: cst.FunctionDef, updated_node: cst.FunctionDef
  ) -> cst.CSTNode:
    if original_node.returns and self._is_any_or_never(
        original_node.returns.annotation
    ):
      return updated_node.with_changes(returns=None)
    return original_node

  def leave_AnnAssign(
      self, original_node: cst.AnnAssign, updated_node: cst.AnnAssign
  ) -> cst.CSTNode:
    if self._is_any_or_never(original_node.annotation):
      return cst.Assign(
          targets=[cst.AssignTarget(target=updated_node.target)],
          value=updated_node.value,
          semicolon=updated_node.semicolon,
      )
    return original_node


class RemoveTrivialTypesTransformer(cst.CSTTransformer):
  """Strips out trivial type of basic-types on variable assignments."""

  def _is_trivial_type(self, annotation: expression.Annotation) -> bool:
    return annotation.annotation is not None and (
        (
            isinstance(annotation.annotation, expression.Name)
            and annotation.annotation.value
            in ("int", "str", "float", "bool", "complex")
        )
        or
        # pytype infers enum members to be literal types, the type
        # annotation in that position is undesirable.
        (
            isinstance(annotation.annotation, expression.Subscript)
            and isinstance(annotation.annotation.value, expression.Name)
            and annotation.annotation.value.value == "Literal"
        )
    )

  def leave_AnnAssign(
      self, original_node: cst.AnnAssign, updated_node: cst.AnnAssign
  ) -> cst.AnnAssign | cst.RemovalSentinel:
    if (
        self._is_trivial_type(original_node.annotation)
        and updated_node.value is None
    ):
      # We need to remove the statement, because otherwise it will be an
      # invalid syntax in python . e.g. `a: str` --> `a`.
      return cst.RemovalSentinel.REMOVE
    return original_node


def merge_sources(*, py: str, pyi: str) -> str:
  try:
    py_cst = cst.parse_module(py)
    pyi_cst = (
        cst.parse_module(pyi)
        .visit(RemoveAnyNeverTransformer())
        .visit(RemoveTrivialTypesTransformer())
    )
    merged_cst = _merge_csts(py_tree=py_cst, pyi_tree=pyi_cst)
    return merged_cst.code
  except Exception as e:  # pylint: disable=broad-except
    raise MergeError(str(e)) from e


class Mode(enum.Enum):
  PRINT = 1
  DIFF = 2
  OVERWRITE = 3


def _get_diff(a, b) -> str:
  a, b = a.split("\n"), b.split("\n")
  diff = difflib.Differ().compare(a, b)
  return "\n".join(diff)


def merge_files(
    *, py_path: str, pyi_path: str, mode: Mode, backup: str | None = None
) -> bool:
  """Merges a .py and a .pyi (experimental: or a pickled pytd) file."""
  _, ext = os.path.splitext(pyi_path)
  if re.fullmatch(r"\.pickled(-\d+)?", ext):
    with open(pyi_path, "rb") as file:
      pyi_src = pytd_utils.Print(pickle_utils.DecodeAst(file.read()).ast)
  else:
    with open(pyi_path) as f:
      pyi_src = f.read()
  return merge_files_src(py_path, pyi_src, mode, backup)


def merge_files_src(
    py_path: str,
    pyi_src: str,
    mode: Mode,
    backup: str | None = None,
) -> bool:
  """Merges annotations from pyi_src content into the .py file py_path."""
  with open(py_path) as f:
    py_src = f.read()
  annotated_src = merge_sources(py=py_src, pyi=pyi_src)
  changed = annotated_src != py_src
  if mode == Mode.PRINT:
    # Always print to stdout even if we haven't changed anything.
    print(annotated_src)
  elif mode == Mode.DIFF and changed:
    diff = _get_diff(py_src, annotated_src)
    print(diff)
  elif mode == Mode.OVERWRITE and changed:
    if backup:
      shutil.copyfile(py_path, f"{py_path}.{backup}")
    with open(py_path, "w") as f:
      f.write(annotated_src)
  return changed


def merge_tree(
    *,
    py_path: str,
    pyi_path: str,
    backup: str | None = None,
    verbose: bool = False,
) -> tuple[list[str], list[tuple[str, MergeError]]]:
  """Merge .py files in a tree with the corresponding .pyi files."""

  errors = []
  changed_files = []

  for root, _, files in os.walk(py_path):
    rel = path_utils.relpath(py_path, root)
    pyi_dir = path_utils.normpath(path_utils.join(pyi_path, rel))
    for f in files:
      if f.endswith(".py"):
        py = path_utils.join(root, f)
        pyi = path_utils.join(pyi_dir, f + "i")
        if path_utils.exists(pyi):
          if verbose:
            print("Merging:", py, end=" ")
          try:
            changed = merge_files(
                py_path=py, pyi_path=pyi, mode=Mode.OVERWRITE, backup=backup
            )
            if changed:
              changed_files.append(py)
            if verbose:
              print("[OK]")
          except MergeError as e:
            errors.append((py, e))
            if verbose:
              print("[FAILED]")
  return changed_files, errors
