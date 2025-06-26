"""Serialize typegraphs into JSON.

Usage:
  from pytype.typegraph import typegraph_serializer
  s = typegraph_serializer.encode_program(ctx.program)
  p = typegraph_serializer.decode_program(s)

You can also call `json` methods directly, using `TypegraphEncoder` and
`object_hook`:
  import json
  from pytype.typegraph import typegraph_serializer
  s = json.dumps(ctx.program, cls=typegraph_serializer.TypegraphEncoder)
  p = json.loads(s, object_hook=typegraph_serializer.object_hook)

We are not interesting in deserialization into a usable cfg.Program. If you
want to use the Program for your own needs later, use pytype as a library.
"""

import dataclasses
import json
from typing import Any, NewType

from pytype.pytd import pytd_utils
from pytype.typegraph import cfg


# All of the IDs are ints. These aliases make the connections between components
# much clearer.
CFGNodeId = NewType("CFGNodeId", int)
BindingId = NewType("BindingId", int)
VariableId = NewType("VariableId", int)


@dataclasses.dataclass
class SerializedCFGNode:
  id: CFGNodeId
  name: str
  incoming: list[CFGNodeId]
  outgoing: list[CFGNodeId]
  bindings: list[BindingId]
  condition: BindingId | None


@dataclasses.dataclass
class SerializedVariable:
  id: VariableId
  bindings: list[BindingId]


@dataclasses.dataclass
class SerializedOrigin:
  where: CFGNodeId
  source_sets: list[list[BindingId]]


@dataclasses.dataclass
class SerializedBinding:
  id: BindingId
  variable: VariableId
  data: Any
  origins: list[SerializedOrigin]


@dataclasses.dataclass
class SerializedQueryStep:
  node: CFGNodeId
  depth: int
  bindings: list[BindingId]


@dataclasses.dataclass
class SerializedQuery:
  id: int
  solver_idx: int
  start_node: CFGNodeId
  end_node: CFGNodeId
  initial_binding_count: int
  shortcircuited: bool
  from_cache: bool
  steps: list[SerializedQueryStep]


@dataclasses.dataclass
class SerializedProgram:
  # Note that cfg_nodes and bindings contain all instances of their respective
  # types that are found in the program, while variables only contains the
  # Variables that have Bindings. This means lookups of variables should be
  # by using `find`, not by direct index access.
  cfg_nodes: list[SerializedCFGNode]
  variables: list[SerializedVariable]
  bindings: list[SerializedBinding]
  entrypoint: CFGNodeId
  queries: list[SerializedQuery]


class TypegraphEncoder(json.JSONEncoder):
  """Implements the JSONEncoder behavior for typegraph objects."""

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self._bindings: dict[int, cfg.Binding] = {}

  def _encode_program(self, program: cfg.Program) -> dict[str, Any]:
    # Surprisingly, program.cfg_nodes and program.variables are not guaranteed
    # to be sorted. Remove this surprise by sorting them here.
    cfg_nodes = sorted(
        [self._encode_cfgnode(n) for n in program.cfg_nodes],
        key=lambda n: n["id"],
    )
    variables = sorted(
        [self._encode_variable(v) for v in program.variables],
        key=lambda v: v["id"],
    )
    # After visiting every Variable, self._bindings contains every Binding.
    bindings = sorted(self._bindings.values(), key=lambda b: b.id)
    return {
        "_type": "Program",
        "cfg_nodes": cfg_nodes,
        "variables": variables,
        "entrypoint": program.entrypoint.id,
        "bindings": [self._encode_binding(b) for b in bindings],
        "queries": self._encode_queries(program),
    }

  def _encode_cfgnode(self, node: cfg.CFGNode) -> dict[str, Any]:
    return {
        "_type": "CFGNode",
        "id": node.id,
        "name": node.name,
        "incoming": [n.id for n in node.incoming],
        "outgoing": [n.id for n in node.outgoing],
        "bindings": [b.id for b in node.bindings],
        "condition": node.condition.id if node.condition else None,
    }

  def _encode_variable(self, variable: cfg.Variable) -> dict[str, Any]:
    self._bindings.update((b.id, b) for b in variable.bindings)
    return {
        "_type": "Variable",
        "id": variable.id,
        "bindings": [b.id for b in variable.bindings],
    }

  def _encode_binding_data(self, binding: cfg.Binding) -> str:
    data = binding.data
    return pytd_utils.Print(data.to_pytd_type()) if data else "None"

  def _encode_binding(self, binding: cfg.Binding) -> dict[str, Any]:
    return {
        "_type": "Binding",
        "id": binding.id,
        "variable": binding.variable.id,
        "data": self._encode_binding_data(binding),
        "origins": [self._encode_origin(o) for o in binding.origins],
    }

  def _encode_origin(self, origin: cfg.Origin) -> dict[str, Any]:
    return {
        "_type": "Origin",
        "where": origin.where.id,
        "source_sets": [[b.id for b in s] for s in origin.source_sets],
    }

  def _encode_queries(self, program: cfg.Program) -> list[dict[str, Any]]:
    """Encodes information about solver queries from a Program's metrics.

    The queries are numbered in the order they were recorded.

    Args:
      program: a cfg.Program.

    Returns:
      A list of dictionaries that correspond to SerializedQuery.
    """
    metrics = program.calculate_metrics()
    solvers = metrics.solver_metrics
    enc_queries = []
    query_id = -1
    for solver_idx, solver in enumerate(solvers):
      for query in solver.query_metrics:
        query_id += 1
        steps = []
        for step in query.steps:
          steps.append({
              "_type": "QueryStep",
              "node": step.node,
              "depth": step.depth,
              "bindings": step.bindings,
          })
        enc_queries.append({
            "_type": "Query",
            "solver_idx": solver_idx,
            "start_node": query.start_node,
            "end_node": query.end_node,
            "initial_binding_count": query.initial_binding_count,
            "shortcircuited": query.shortcircuited,
            "from_cache": query.from_cache,
            "steps": steps,
        })
    return enc_queries

  def default(self, o):
    if isinstance(o, cfg.Program):
      return self._encode_program(o)
    elif isinstance(o, cfg.CFGNode):
      return self._encode_cfgnode(o)
    elif isinstance(o, cfg.Variable):
      return self._encode_variable(o)
    elif isinstance(o, cfg.Binding):
      return self._encode_binding(o)
    elif isinstance(o, cfg.Origin):
      return self._encode_origin(o)
    else:
      return super().default(o)


_TYP_MAP = {
    "Program": SerializedProgram,
    "CFGNode": SerializedCFGNode,
    "Variable": SerializedVariable,
    "Binding": SerializedBinding,
    "Origin": SerializedOrigin,
    "QueryStep": SerializedQueryStep,
    "Query": SerializedQuery,
}


def _decode(obj):
  typ = obj.pop("_type")
  return _TYP_MAP[typ](**obj)


def object_hook(obj: dict[str, Any]) -> Any:
  """An object hook for json.load that produces serialized CFG objects."""
  if "_type" in obj:
    return _decode(obj)
  return obj


def encode_program(program: cfg.Program) -> str:
  return json.dumps(program, cls=TypegraphEncoder)


def decode_program(json_str: str) -> SerializedProgram:
  prog = json.loads(json_str, object_hook=object_hook)
  assert isinstance(prog, SerializedProgram)
  return prog


def to_serialized_program(program: cfg.Program) -> SerializedProgram:
  return decode_program(encode_program(program))
