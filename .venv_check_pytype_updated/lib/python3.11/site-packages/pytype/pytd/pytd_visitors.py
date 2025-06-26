"""Visitor(s) for walking ASTs.

This module contains broadly useful basic visitors. Visitors that are more
specialized to pytype are in visitors.py. If you see a visitor there that you'd
like to use, feel free to propose moving it here.
"""

from pytype.pytd import base_visitor
from pytype.pytd import pytd


# TODO(rechen): IsNamedTuple is being used to disable visitors that shouldn't
# operate on generated classes. Should we do the same for dataclasses and attrs?
def IsNamedTuple(node: pytd.Class):
  return any(
      base.name in ("collections.namedtuple", "typing.NamedTuple")
      for base in node.bases
  )


class CanonicalOrderingVisitor(base_visitor.Visitor):
  """Visitor for converting ASTs back to canonical (sorted) ordering.

  Note that this visitor intentionally does *not* sort a function's signatures,
  as the signature order determines lookup order.
  """

  def VisitTypeDeclUnit(self, node):
    return pytd.TypeDeclUnit(
        name=node.name,
        constants=tuple(sorted(node.constants)),
        type_params=tuple(sorted(node.type_params)),
        functions=tuple(sorted(node.functions)),
        classes=tuple(sorted(node.classes)),
        aliases=tuple(sorted(node.aliases)),
    )

  def _PreserveConstantsOrdering(self, node):
    # If we have a dataclass-like decorator we need to preserve the order of the
    # class attributes, otherwise inheritance will not work correctly.
    if any(
        x.name in ("attr.s", "dataclasses.dataclass") for x in node.decorators
    ):
      return True
    # The order of a namedtuple's fields should always be preserved.
    return IsNamedTuple(node)

  def VisitClass(self, node):
    if self._PreserveConstantsOrdering(node):
      constants = node.constants
    else:
      constants = sorted(node.constants)
    return pytd.Class(
        name=node.name,
        keywords=node.keywords,
        bases=node.bases,
        methods=tuple(sorted(node.methods)),
        constants=tuple(constants),
        decorators=tuple(sorted(node.decorators)),
        classes=tuple(sorted(node.classes)),
        slots=tuple(sorted(node.slots)) if node.slots is not None else None,
        template=node.template,
    )

  def VisitSignature(self, node):
    return node.Replace(
        template=tuple(sorted(node.template)),
        exceptions=tuple(sorted(node.exceptions)),
    )

  def VisitUnionType(self, node):
    return pytd.UnionType(tuple(sorted(node.type_list)))


class ClassTypeToNamedType(base_visitor.Visitor):
  """Change all ClassType objects to NameType objects."""

  def VisitClassType(self, node):
    return pytd.NamedType(node.name)


class CollectTypeParameters(base_visitor.Visitor):
  """Visitor that accumulates type parameters in its "params" attribute."""

  def __init__(self):
    super().__init__()
    self._seen = set()
    self.params = []

  def EnterTypeParameter(self, p):
    if p.name not in self._seen:
      self.params.append(p)
      self._seen.add(p.name)

  def EnterParamSpec(self, p):
    self.EnterTypeParameter(p)


class ExtractSuperClasses(base_visitor.Visitor):
  """Visitor for extracting all superclasses (i.e., the class hierarchy).

  When called on a TypeDeclUnit, this yields a dictionary mapping pytd.Class
  to lists of pytd.Type.
  """

  def __init__(self):
    super().__init__()
    self._superclasses = {}

  def _Key(self, node):
    # This method should be implemented by subclasses.
    return node

  def VisitTypeDeclUnit(self, module):
    del module
    return self._superclasses

  def EnterClass(self, cls):
    bases = []
    for p in cls.bases:
      base = self._Key(p)
      if base is not None:
        bases.append(base)
    self._superclasses[self._Key(cls)] = bases


class RenameModuleVisitor(base_visitor.Visitor):
  """Renames a TypeDeclUnit."""

  def __init__(self, old_module_name, new_module_name):
    """Constructor.

    Args:
      old_module_name: The old name of the module as a string, e.g.
        "foo.bar.module1"
      new_module_name: The new name of the module as a string, e.g.
        "barfoo.module2"

    Raises:
      ValueError: If the old_module name is an empty string.
    """
    super().__init__()
    if not old_module_name:
      raise ValueError("old_module_name must be a non empty string.")
    assert not old_module_name.endswith(".")
    assert not new_module_name.endswith(".")
    self._module_name = new_module_name
    self._old = old_module_name + "." if old_module_name else ""
    self._new = new_module_name + "." if new_module_name else ""

  def _MaybeNewName(self, name):
    """Decides if a name should be replaced.

    Args:
      name: A name for which a prefix should be changed.

    Returns:
      If name is local to the module described by old_module_name the
      old_module_part will be replaced by new_module_name and returned,
      otherwise node.name will be returned.
    """
    if not name:
      return name
    if name == self._old[:-1]:
      return self._module_name
    before, match, after = name.partition(self._old)
    if match and not before:
      return self._new + after
    else:
      return name

  def _ReplaceModuleName(self, node):
    new_name = self._MaybeNewName(node.name)
    if new_name != node.name:
      return node.Replace(name=new_name)
    else:
      return node

  def VisitClassType(self, node):
    new_name = self._MaybeNewName(node.name)
    if new_name != node.name:
      return pytd.ClassType(new_name, node.cls)
    else:
      return node

  def VisitTypeDeclUnit(self, node):
    return node.Replace(name=self._module_name)

  def VisitTypeParameter(self, node):
    new_scope = self._MaybeNewName(node.scope)
    if new_scope != node.scope:
      return node.Replace(scope=new_scope)
    return node

  def VisitParamSpec(self, node):
    new_scope = self._MaybeNewName(node.scope)
    if new_scope != node.scope:
      return node.Replace(scope=new_scope)
    return node

  VisitConstant = _ReplaceModuleName  # pylint: disable=invalid-name
  VisitAlias = _ReplaceModuleName  # pylint: disable=invalid-name
  VisitClass = _ReplaceModuleName  # pylint: disable=invalid-name
  VisitFunction = _ReplaceModuleName  # pylint: disable=invalid-name
  VisitStrictType = _ReplaceModuleName  # pylint: disable=invalid-name
  VisitModule = _ReplaceModuleName  # pylint: disable=invalid-name
  VisitNamedType = _ReplaceModuleName  # pylint: disable=invalid-name
