"""Base class for visitors."""

import re
from typing import Any

import msgspec
from pytype.pytd import pytd
from pytype.typegraph import cfg_utils

# A convenient value for unchecked_node_classnames if a visitor wants to
# use unchecked nodes everywhere.
ALL_NODE_NAMES = type(
    "contains_everything", (), {"__contains__": lambda *args: True}
)()


class _NodeClassInfo:
  """Representation of a node class in the graph."""

  def __init__(self, cls):
    self.cls = cls  # The class object.
    self.name = cls.__name__
    # The set of NodeClassInfo objects that may appear below this particular
    # type of node. Initially empty, filled in by examining child fields.
    self.outgoing = set()


def _FindNodeClasses():
  """Yields _NodeClassInfo objects for each node found in pytd."""
  for name in dir(pytd):
    value = getattr(pytd, name)
    if (
        isinstance(value, type)
        and issubclass(value, pytd.Node)
        and value is not pytd.Node
        and value is not pytd.Type
    ):
      yield _NodeClassInfo(value)


_IGNORED_TYPES = frozenset([str, bool, int, type(None), Any])
_ancestor_map = None  # Memoized ancestors map.


def _GetChildTypes(node_classes, cls: Any):
  """Get all the types that can be in a node's subtree."""

  types = set()

  def AddType(t: Any):
    if hasattr(t, "__args__"):
      # Tuple[...] and Union[...] store their contained types in __args__
      for x in t.__args__:
        if x is not Ellipsis:
          AddType(x)
      return
    if hasattr(t, "__forward_arg__"):
      # __forward_arg__ is the runtime representation of late annotations
      t = t.__forward_arg__
    if isinstance(t, str) and t in node_classes:
      types.add(node_classes[t].cls)
    else:
      types.add(t)

  for field in msgspec.structs.fields(cls):
    AddType(field.type)

  # Verify that all late types have been converted.
  for x in types:
    assert isinstance(x, type) or x == Any

  return types


def _GetAncestorMap():
  """Return a map of node class names to a set of ancestor class names."""

  global _ancestor_map
  if _ancestor_map is None:
    # Map from name to _NodeClassInfo.
    node_classes = {i.name: i for i in _FindNodeClasses()}

    # Update _NodeClassInfo.outgoing based on children.
    for info in node_classes.values():
      for allowed in _GetChildTypes(node_classes, info.cls):
        if allowed in _IGNORED_TYPES:
          pass
        elif allowed.__module__ == "pytype.pytd.pytd":
          # All subclasses of the type are allowed.
          info.outgoing.update(
              [i for i in node_classes.values() if issubclass(i.cls, allowed)]
          )
        else:
          # This means we have a child type that is unknown. If it is a node
          # then make sure _FindNodeClasses() can discover it. If it is not a
          # node, then add the typename to _IGNORED_TYPES.
          raise AssertionError(f"Unknown child type on {info.name}: {allowed}")

    predecessors = cfg_utils.compute_predecessors(node_classes.values())
    # Convert predecessors keys and values to use names instead of info objects.
    get_names = lambda v: {n.name for n in v}
    _ancestor_map = {k.name: get_names(v) for k, v in predecessors.items()}
  return _ancestor_map


class Visitor:
  """Base class for visitors.

  Each class inheriting from visitor SHOULD have a fixed set of methods,
  otherwise it might break the caching in this class.

  Attributes:
    visits_all_node_types: Whether the visitor can visit every node type.
    unchecked_node_names: Contains the names of node classes that are unchecked
      when constructing a new node from visited children.  This is useful if a
      visitor returns data in part or all of its walk that would violate node
      preconditions.
    enter_functions: A dictionary mapping node class names to the corresponding
      Enter functions.
    visit_functions: A dictionary mapping node class names to the corresponding
      Visit functions.
    leave_functions: A dictionary mapping node class names to the corresponding
      Leave functions.
    visit_class_names: A set of node class names that must be visited.  This is
      constructed based on the enter/visit/leave functions and precondition data
      about legal ASTs.  As an optimization, the visitor will only visit nodes
      under which some actionable node can appear.
  """

  # The old_node attribute contains a copy of the node before its children were
  # visited. It has the same type as the node currently being visited.
  old_node: Any

  visits_all_node_types = False
  unchecked_node_names = set()

  _visitor_functions_cache = {}

  def __init__(self):
    cls = self.__class__

    # The set of method names for each visitor implementation is assumed to
    # be fixed. Therefore this introspection can be cached.
    if cls in Visitor._visitor_functions_cache:
      enter_fns, visit_fns, leave_fns, visit_class_names = (
          Visitor._visitor_functions_cache[cls]
      )
    else:
      enter_fns = {}
      enter_prefix = "Enter"
      enter_len = len(enter_prefix)

      visit_fns = {}
      visit_prefix = "Visit"
      visit_len = len(visit_prefix)

      leave_fns = {}
      leave_prefix = "Leave"
      leave_len = len(leave_prefix)

      for attrib in dir(cls):
        if attrib.startswith(enter_prefix):
          enter_fns[attrib[enter_len:]] = getattr(cls, attrib)
        elif attrib.startswith(visit_prefix):
          visit_fns[attrib[visit_len:]] = getattr(cls, attrib)
        elif attrib.startswith(leave_prefix):
          leave_fns[attrib[leave_len:]] = getattr(cls, attrib)

      ancestors = _GetAncestorMap()
      visit_class_names = set()
      # A custom Enter/Visit/Leave requires visiting all types of nodes.
      visit_all = (
          cls.Enter != Visitor.Enter
          or cls.Visit != Visitor.Visit
          or cls.Leave != Visitor.Leave
      )
      for node in set(enter_fns) | set(visit_fns) | set(leave_fns):
        if node in ancestors:
          visit_class_names.update(ancestors[node])
        elif node:
          # Visiting an unknown non-empty node means the visitor has defined
          # behavior on nodes that are unknown to the ancestors list.
          if node == "StrictType":
            # This special case is here because pytd.type_match defines an extra
            # StrictType node, and pytd.printer.PrintVisitor has a visitor to
            # handle it.
            visit_all = True
          elif cls.__module__ == "__main__" or re.fullmatch(
              r".*(_test|test_[^\.]+)", cls.__module__
          ):
            # We are running test code or something else that is defining its
            # own pytd nodes directly in a top-level python file.
            visit_all = True
          else:
            raise AssertionError(f"Unknown node type: {node} {cls!r}")
      if visit_all:
        visit_class_names = ALL_NODE_NAMES
      Visitor._visitor_functions_cache[cls] = (
          enter_fns,
          visit_fns,
          leave_fns,
          visit_class_names,
      )

    self.enter_functions = enter_fns
    self.visit_functions = visit_fns
    self.leave_functions = leave_fns
    self.visit_class_names = visit_class_names

  def Enter(self, node, *args, **kwargs):
    return self.enter_functions[node.__class__.__name__](
        self, node, *args, **kwargs
    )

  def Visit(self, node, *args, **kwargs):
    return self.visit_functions[node.__class__.__name__](
        self, node, *args, **kwargs
    )

  def Leave(self, node, *args, **kwargs):
    self.leave_functions[node.__class__.__name__](self, node, *args, **kwargs)
