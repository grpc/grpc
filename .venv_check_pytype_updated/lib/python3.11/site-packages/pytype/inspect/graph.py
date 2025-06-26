"""Inspect the CFG and typegraph."""

import logging
import subprocess

import networkx as nx


log = logging.getLogger(__name__)


def obj_key(n):
  nid = n.id if hasattr(n, "id") else id(n)
  return n.__class__.__name__ + str(nid)


def obj_repr(n):
  return repr(n.data)[:10]


class TypeGraph:
  """Networkx graph builder."""

  def __init__(self, program, ignored, only_cfg=False):
    self.graph = nx.MultiDiGraph()
    self._add_cfg(program, ignored)
    if not only_cfg:
      self._add_variables(program, ignored)

  def add_node(self, obj, **kwargs):
    self.graph.add_node(obj_key(obj), **kwargs)

  def add_edge(self, obj1, obj2, **kwargs):
    self.graph.add_edge(obj_key(obj1), obj_key(obj2), **kwargs)

  def to_dot(self):
    return nx.nx_pydot.to_pydot(self.graph).to_string()

  def _add_cfg(self, program, ignored):
    """Add program cfg nodes."""

    for node in program.cfg_nodes:
      if node in ignored:
        continue
      self.add_node(
          node, label=f"<{node.id}>{node.name}", shape="polygon", sides=4
      )
      for other in node.outgoing:
        self.add_edge(node, other, penwidth=2.0)

  def _add_variables(self, program, ignored):
    """A dd program variables and bindings."""

    def _is_constant(val):
      return all(origin.where == program.entrypoint for origin in val.origins)

    for variable in program.variables:
      if variable.id in ignored:
        continue
      # Ignore "boring" values (a.k.a. constants)
      if all(_is_constant(value) for value in variable.bindings):
        continue
      self.add_node(
          variable,
          label=f"v{variable.id}",
          shape="polygon",
          sides=4,
          distortion=0.1,
      )
      for val in variable.bindings:
        label = f"{obj_repr(val)}@0x{id(val.data):x}"
        color = "white" if val.origins else "red"
        self.add_node(val, label=label, fillcolor=color)
        self.add_edge(variable, val, arrowhead="none")
        for origin in val.origins:
          if origin.where == program.entrypoint:
            continue
          for srcs in origin.source_sets:
            self.add_node(srcs, label="")
            self.add_edge(val, srcs, color="pink", arrowhead="none", weight=40)
            if origin.where not in ignored:
              self.add_edge(
                  origin.where, srcs, arrowhead="none", style="dotted", weight=5
              )
            for src in srcs:
              self.add_edge(src, srcs, color="lightblue", weight=2)


def write_svg_from_dot(svg_file, dot):
  with subprocess.Popen(
      ["/usr/bin/dot", "-T", "svg", "-o", svg_file],
      stdin=subprocess.PIPE,
      universal_newlines=True,
  ) as proc:
    (_, stderr) = proc.communicate(dot)
  if stderr:
    log.info("Failed to create %s: %s", svg_file, stderr)
