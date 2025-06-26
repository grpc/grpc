"""Base node class used to represent immutable trees.

All node classes should be 'attr' classes inheriting from Node, which defines
field iteration and comparison methods.

For the concrete node types, see pytd/pytd.py

This module also defines a visitor interface which works over immutable trees,
replacing a node with a new instance if any of its fields have changed. See the
documentation in the various visitor methods below for details.

For examples of visitors, see pytd/visitors.py
"""

from typing import Any, ClassVar, TYPE_CHECKING

import msgspec
from pytype import metrics

if TYPE_CHECKING:
  _Struct: type[Any]
else:
  _Struct = msgspec.Struct


class Node(
    _Struct,
    frozen=True,
    tag=True,
    tag_field="_struct_type",
    kw_only=True,
    omit_defaults=True,
    cache_hash=True,
):
  """Base Node class."""

  # We pretend that `name` is a ClassVar so that msgspec treats it as a struct
  # field only when it is defined in a subclass.
  name: ClassVar[str] = ""

  def __iter__(self):
    for name in self.__struct_fields__:
      yield getattr(self, name)

  def _ToTuple(self):
    """Returns a tuple of the stringified fields of self as a sort key."""
    return tuple((x.__class__.__name__, str(x)) for x in self)

  def __lt__(self, other):
    """Smaller than other node? Define so we can have deterministic ordering."""
    if self is other:
      return False
    elif self.__class__ is other.__class__:
      return tuple.__lt__(self._ToTuple(), other._ToTuple())
    else:
      return self.__class__.__name__ < other.__class__.__name__

  def __gt__(self, other):
    """Larger than other node? Define so we can have deterministic ordering."""
    if self is other:
      return False
    elif self.__class__ is other.__class__:
      return tuple.__gt__(self._ToTuple(), other._ToTuple())
    else:
      return self.__class__.__name__ > other.__class__.__name__

  def __le__(self, other):
    return self == other or self < other

  def __ge__(self, other):
    return self == other or self > other

  def IterChildren(self):
    for name in self.__struct_fields__:
      yield name, getattr(self, name)

  def Visit(self, visitor, *args, **kwargs):
    """Visitor interface for transforming a tree of nodes to a new tree.

    You can pass a visitor, and callback functions on that visitor will be
    called for all nodes in the tree. Note that nodes are also allowed to
    be stored in lists and as the values of dictionaries, as long as these
    lists/dictionaries are stored in the named fields of the Node class.
    It's possible to overload the Visit function on Nodes, to do your own
    processing.

    Arguments:
      visitor: An instance of a visitor for this tree. For every node type you
        want to transform, this visitor implements a "Visit<Classname>"
        function named after the class of the node this function should
        target. Note that <Classname> is the *actual* class of the node, so
        if you subclass a Node class, visitors for the superclasses will *not*
        be triggered anymore. Also, visitor callbacks are only triggered
        for subclasses of Node.
      *args: Passed to the visitor callback.
      **kwargs: Passed to the visitor callback.

    Returns:
      Transformed version of this node.
    """
    return _Visit(self, visitor, *args, **kwargs)

  def Replace(self, **kwargs):
    return msgspec.structs.replace(self, **kwargs)


# The set of visitor names currently being processed.
_visiting = set()


def _Visit(node, visitor, *args, **kwargs):
  """Visit the node."""
  name = type(visitor).__name__
  recursive = name in _visiting
  _visiting.add(name)

  start = metrics.get_cpu_clock()
  try:
    return _VisitNode(node, visitor, *args, **kwargs)
  finally:
    if not recursive:
      _visiting.remove(name)
      elapsed = metrics.get_cpu_clock() - start
      metrics.get_metric("visit_" + name, metrics.Distribution).add(elapsed)
      if _visiting:
        metrics.get_metric(
            "visit_nested_" + name, metrics.Distribution).add(elapsed)


def _VisitNode(node, visitor, *args, **kwargs):
  """Transform a node and all its children using a visitor.

  This will iterate over all children of this node, and also process certain
  things that are not nodes. The latter are either tuples, which will have their
  elements visited, or primitive types, which will be returned as-is.

  Args:
    node: The node to transform. Either an actual instance of Node, or a
          tuple found while scanning a node tree, or any other type (which will
          be returned unmodified).
    visitor: The visitor to apply. If this visitor has a "Visit<Name>" method,
          with <Name> the name of the Node class, a callback will be triggered,
          and the transformed version of this node will be whatever the callback
          returned.  Before calling the Visit callback, the following
          attribute(s) on the Visitor class will be populated:
            visitor.old_node: The node before the child nodes were visited.

          Additionally, if the visitor has a "Enter<Name>" method, that method
          will be called on the original node before descending into it. If
          "Enter<Name>" returns False, the visitor will not visit children of
          this node. If "Enter<name>" returns a set of field names, those field
          names will not be visited. Otherwise, "Enter<Name>" should return
          None, to indicate that nodes will be visited normally.

          "Enter<Name>" is called pre-order; "Visit<Name> and "Leave<Name>" are
          called post-order.  A counterpart to "Enter<Name>" is "Leave<Name>",
          which is intended for any clean-up that "Enter<Name>" needs (other
          than that, it's redundant, and could be combined with "Visit<Name>").
    *args: Passed to visitor callbacks.
    **kwargs: Passed to visitor callbacks.
  Returns:
    The transformed Node (which *may* be the original node but could be a new
     node, even if the contents are the same).
  """
  node_class = node.__class__
  if node_class is tuple:
    changed = False
    new_children = []
    for child in node:
      new_child = _VisitNode(child, visitor, *args, **kwargs)
      if new_child is not child:
        changed = True
      new_children.append(new_child)
    if changed:
      # Since some of our children changed, instantiate a new node.
      return node_class(new_children)
    else:
      # Optimization: if we didn't change any of the children, keep the entire
      # object the same.
      return node
  elif not isinstance(node, Node):
    return node

  # At this point, assume node is a Node.
  node_class_name = node_class.__name__
  if node_class_name not in visitor.visit_class_names:
    return node

  skip_children = set()
  if node_class_name in visitor.enter_functions:
    # The visitor wants to be informed that we're descending into this part
    # of the tree.
    status = visitor.Enter(node, *args, **kwargs)
    if status is False:  # pylint: disable=g-bool-id-comparison
      # Don't descend if Enter<Node> explicitly returns False, but not None,
      # since None is the default return of Python functions.
      return node
    elif isinstance(status, set):
      # If we are given a set of field names, do not visit those fields
      skip_children = status
    else:
      # Any other value returned from Enter is ignored, so check:
      assert status is None, repr((node_class_name, status))

  changed = False
  new_children = []
  for name, child in node.IterChildren():
    if name in skip_children:
      new_child = child
    else:
      new_child = _VisitNode(child, visitor, *args, **kwargs)
      if new_child is not child:
        changed = True
    new_children.append(new_child)
  if changed:
    new_node = node_class(*new_children)
  else:
    new_node = node

  visitor.old_node = node
  # Now call the user supplied callback(s), if they exist.
  if (visitor.visits_all_node_types or
      node_class_name in visitor.visit_functions):
    new_node = visitor.Visit(new_node, *args, **kwargs)
  if node_class_name in visitor.leave_functions:
    visitor.Leave(node, *args, **kwargs)

  del visitor.old_node
  return new_node
