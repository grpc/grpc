"""Utilities for working with the CFG."""

import collections
from collections.abc import Iterable, Sequence
import itertools
from typing import Protocol, TypeVar


# Limit on how many argument combinations we allow before aborting.
# For a sample of 16664 (sane) source files without imports, these are the
# quantiles that were below the given number of argument combinations:
#     50%  75%  90%  99%  99.9%  99.99%
#       1    3    6   57    809    9638
# We also know of two problematic files, with 4,800 and 10,000
# combinations, respectively.
# So pick a number that excludes as few files as possible (0.1%) but also
# cuts off problematic files, with a comfortable margin.
DEEP_VARIABLE_LIMIT = 1024


def variable_product(variables):
  """Take the Cartesian product of a number of Variables.

  Args:
    variables: A sequence of Variables.

  Returns:
    A list of lists of Values, where each sublist has one element from each
    of the given Variables.
  """
  return itertools.product(*(v.bindings for v in variables))


def _variable_product_items(variableitems, complexity_limit):
  """Take the Cartesian product of a list of (key, value) tuples.

  See variable_product_dict below.

  Args:
    variableitems: A dict mapping object to cfg.Variable.
    complexity_limit: A counter that tracks how many combinations we've yielded
      and aborts if we go over the limit.

  Yields:
    A sequence of [(key, cfg.Binding), ...] lists.
  """
  variableitems_iter = iter(variableitems)
  try:
    headkey, headvar = next(variableitems_iter)
  except StopIteration:
    yield []
  else:
    for tail in _variable_product_items(variableitems_iter, complexity_limit):
      for headvalue in headvar.bindings:
        complexity_limit.inc()
        yield [(headkey, headvalue)] + tail


class TooComplexError(Exception):
  """Thrown if we determine that something in our program is too complex."""


class ComplexityLimit:
  """A class that raises TooComplexError if we hit a limit."""

  def __init__(self, limit):
    self.limit = limit
    self.count = 0

  def inc(self, add=1):
    self.count += add
    if self.count >= self.limit:
      raise TooComplexError()


def deep_variable_product(variables, limit=DEEP_VARIABLE_LIMIT):
  """Take the deep Cartesian product of a list of Variables.

  For example:
    x1.children = {v2, v3}
    v1 = {x1, x2}
    v2 = {x3}
    v3 = {x4, x5}
    v4 = {x6}
  then
    deep_variable_product([v1, v4]) will return:
      [[x1, x3, x4, x6],
       [x1, x3, x5, x6],
       [x2, x6]]
  .
  Args:
    variables: A sequence of Variables.
    limit: How many results we allow before aborting.

  Returns:
    A list of lists of Values, where each sublist has one Value from each
    of the corresponding Variables and the Variables of their Values' children.

  Raises:
    TooComplexError: If we expanded too many values.
  """
  return _deep_values_list_product(
      [v.bindings for v in variables], set(), ComplexityLimit(limit)
  )


def _deep_values_list_product(values_list, seen, complexity_limit):
  """Take the deep Cartesian product of a list of list of Values."""
  result = []
  for row in itertools.product(*(values for values in values_list if values)):
    extra_params = [
        value
        for entry in row
        if entry not in seen  # pylint: disable=g-complex-comprehension
        for value in entry.data.unique_parameter_values()
    ]
    extra_values = extra_params and _deep_values_list_product(
        extra_params, seen.union(row), complexity_limit
    )
    if extra_values:
      for new_row in extra_values:
        result.append(row + new_row)
    else:
      complexity_limit.inc()
      result.append(row)
  return result


def variable_product_dict(variabledict, limit=DEEP_VARIABLE_LIMIT):
  """Take the Cartesian product of variables in the values of a dict.

  This Cartesian product is taken using the dict keys as the indices into the
  input and output dicts. So:
    variable_product_dict({"x": Variable(a, b), "y": Variable(c, d)})
      ==
    [{"x": a, "y": c}, {"x": a, "y": d}, {"x": b, "y": c}, {"x": b, "y": d}]
  This is exactly analogous to a traditional Cartesian product except that
  instead of trying each possible value of a numbered position, we are trying
  each possible value of a named position.

  Args:
    variabledict: A dict with variable values.
    limit: How many results to allow before aborting.

  Returns:
    A list of dicts with Value values.
  """
  return [
      dict(d)
      for d in _variable_product_items(
          variabledict.items(), ComplexityLimit(limit)
      )
  ]


def merge_variables(program, node, variables):
  """Create a combined Variable for a list of variables.

  The purpose of this function is to create a final result variable for
  functions that return a list of "temporary" variables. (E.g. function
  calls).

  Args:
    program: A cfg.Program instance.
    node: The current CFG node.
    variables: A list of cfg.Variables.

  Returns:
    A cfg.Variable.
  """
  if not variables:
    return program.NewVariable()  # return empty var
  elif all(v is variables[0] for v in variables):
    return variables[0].AssignToNewVariable(node)
  else:
    v = program.NewVariable()
    for r in variables:
      v.PasteVariable(r, node)
    return v


def merge_bindings(program, node, bindings):
  """Create a combined Variable for a list of bindings.

  Args:
    program: A cfg.Program instance.
    node: The current CFG node.
    bindings: A list of cfg.Bindings.

  Returns:
    A cfg.Variable.
  """
  v = program.NewVariable()
  for b in bindings:
    v.PasteBinding(b, node)
  return v


def walk_binding(binding, keep_binding=lambda _: True):
  """Helper function to walk a binding's origins.

  Args:
    binding: A cfg.Binding.
    keep_binding: Optionally, a function, cfg.Binding -> bool, specifying
      whether to keep each binding found.

  Yields:
    A cfg.Origin. The caller must send the origin back into the generator. To
    stop exploring the origin, send None back.
  """
  bindings = [binding]
  seen = set()
  while bindings:
    b = bindings.pop(0)
    if b in seen or not keep_binding(b):
      continue
    seen.add(b)
    for o in b.origins:
      o = yield o
      if o:
        bindings.extend(itertools.chain(*o.source_sets))


class PredecessorNode(Protocol):
  outgoing: "Iterable[PredecessorNode]"


_PredecessorNode = TypeVar("_PredecessorNode", bound=PredecessorNode)


def compute_predecessors(
    nodes: Iterable[_PredecessorNode],
) -> dict[_PredecessorNode, set[_PredecessorNode]]:
  """Build a transitive closure.

  For a list of nodes, compute all the predecessors of each node.

  Args:
    nodes: A list of nodes or blocks.

  Returns:
    A dictionary that maps each node to a set of all the nodes that can reach
    that node.
  """
  # Our CFGs are reflexive: Every node can reach itself.
  predecessors = {n: {n} for n in nodes}
  discovered = set()  # Nodes found below some start node.

  # Start at a possible root and follow outgoing edges to update predecessors as
  # needed. Since the maximum number of times a given edge is processed is |V|,
  # the worst-case runtime is |V|*|E|. However, these graphs are typically
  # trees, so the usual runtime is much closer to |E|. Compared to using
  # Floyd-Warshall (|V|^3), this brings down the execution time on some files
  # from about 30s to less than 7s.
  for start in nodes:
    if start in discovered:
      # We have already seen this node from a previous start, do nothing.
      continue
    unprocessed = [(start, n) for n in start.outgoing]
    while unprocessed:
      from_node, node = unprocessed.pop(0)
      node_predecessors = predecessors[node]
      length_before = len(node_predecessors)
      # Add the predecessors of from_node to this node's predecessors
      node_predecessors |= predecessors[from_node]
      if length_before != len(node_predecessors):
        # All of the nodes directly reachable from this one need their
        # predecessors updated
        unprocessed.extend((node, n) for n in node.outgoing)
        discovered.add(node)

  return predecessors


class OrderableNode(PredecessorNode, Protocol):
  id: int


_OrderableNode = TypeVar("_OrderableNode", bound=OrderableNode)


def order_nodes(nodes: Sequence[_OrderableNode]) -> list[_OrderableNode]:
  """Build an ancestors first traversal of CFG nodes.

  This guarantees that at least one predecessor of a block is scheduled before
  the block itself, and it also tries to schedule as many of them before the
  block as possible (so e.g. if two branches merge in a node, it prefers to
  process both the branches before that node).

  Args:
    nodes: A list of nodes or blocks. They have two attributes: "id" (an int to
      enable deterministic sorting) and "outgoing" (a list of nodes).

  Returns:
    A list of nodes in the proper order.
  """
  if not nodes:
    return []
  root = nodes[0]
  predecessor_map = compute_predecessors(nodes)
  dead = {
      node
      for node, predecessors in predecessor_map.items()
      if root not in predecessors
  }
  queue = {root: predecessor_map[root]}
  order = []
  seen = set()
  while queue:
    # Find node with minimum amount of predecessors that's connected to a node
    # we already processed.
    _, _, node = min(
        (len(predecessors), node.id, node)
        for node, predecessors in queue.items()
    )
    del queue[node]
    if node in seen:
      continue
    order.append(node)
    seen.add(node)
    # Remove this node from the predecessors of all nodes after it.
    for _, predecessors in queue.items():
      predecessors.discard(node)
    # Potentially schedule nodes we couldn't reach before:
    for n in node.outgoing:
      if n not in queue:
        queue[n] = predecessor_map[n] - seen

  # check that we don't have duplicates and that we didn't miss anything:
  assert len(set(order) | dead) == len(set(nodes))

  return order


def topological_sort(nodes):
  """Sort a list of nodes topologically.

  This will order the nodes so that any node that appears in the "incoming"
  list of another node n2 will appear in the output before n2. It assumes that
  the graph doesn't have any cycles.
  If there are multiple ways to sort the list, a random one is picked.

  Args:
    nodes: A sequence of nodes. Each node may have an attribute "incoming", a
      list of nodes (every node in this list needs to be in "nodes"). If
      "incoming" is not there, it's assumed to be empty. The list of nodes can't
      have duplicates.

  Yields:
    The nodes in their topological order.
  Raises:
    ValueError: If the graph contains a cycle.
  """
  incoming = {node: set(getattr(node, "incoming", ())) for node in nodes}
  outgoing = collections.defaultdict(set)
  for node in nodes:
    for inc in incoming[node]:
      outgoing[inc].add(node)
  stack = [node for node in nodes if not incoming[node]]
  for _ in nodes:
    if not stack:
      raise ValueError("Circular graph")
    leaf = stack.pop()
    yield leaf
    for out in outgoing[leaf]:
      incoming[out].remove(leaf)
      if not incoming[out]:
        stack.append(out)
  assert not stack
