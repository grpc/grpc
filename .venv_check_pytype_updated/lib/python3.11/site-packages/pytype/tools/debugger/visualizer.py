"""Library for creating visualizations of the pytype typegraph.

This is intended as a convenient wrapper for the jinja2 template. All it does is
load the template, encode the program, then call template.render.

Because the visualizer template uses an `include` statement, a full
jinja2.Environment needs to be created. generate() accepts a jinja2.Loader in
order to support different execution environments.
"""

import json

import jinja2

from pytype.blocks import block_serializer
from pytype.blocks import blocks
from pytype.typegraph import cfg
from pytype.typegraph import typegraph_serializer

_TYPEGRAPH_TEMPLATE_NAME = "visualizer.html.jinja2"
_BLOCKGRAPH_TEMPLATE_NAME = "block_visualizer.html.jinja2"

# If it's good enough for google.com, it's good enough for us.
_CYTOSCAPE_URL = "https://www.gstatic.com/external_hosted/cytoscape/cytoscape.js"
_DAGRE_URL = "https://www.gstatic.com/external_hosted/dagre/dagre.js"
_CYTOSCAPE_DAGRE_URL = "https://www.gstatic.com/external_hosted/cytoscape-dagre/cytoscape-dagre.js"


def _generate_visualization(
    template_file: str,
    loader: jinja2.BaseLoader,
    **kwargs
) -> str:
  """Generate the visualization webpage.

  Args:
    template_file: str. A jinja2 template filename.
    loader: jinja2.BaseLoader. The loader needs to be able to load files in this
      file's directory.
    **kwargs: Additional args passed on to the template.

  Returns:
    str. The rendered visualization page.
  """
  env = jinja2.Environment(loader=loader)
  template = env.get_template(template_file)
  return template.render(
      cytoscape_url=_CYTOSCAPE_URL,
      dagre_url=_DAGRE_URL,
      cytoscape_dagre_url=_CYTOSCAPE_DAGRE_URL,
      **kwargs
  )


def generate_typegraph(
    program: cfg.Program,
    var_table: dict[int, str],
    loader: jinja2.BaseLoader,
) -> str:
  """Generate the visualization webpage.

  Args:
    program: cfg.Program. The instance of the program to visualize.
    var_table: dict[int, str]. A mapping of cfg.Variable IDs to names.
    loader: A jinja2 loader

  Returns:
    str. The rendered visualization page.
  """
  encoder = typegraph_serializer.TypegraphEncoder()
  enc_prog = encoder.default(program)
  return _generate_visualization(
      template_file=_TYPEGRAPH_TEMPLATE_NAME,
      loader=loader,
      program=json.dumps(enc_prog),
      query_table=enc_prog["queries"],
      var_table=var_table,
  )


def generate_block_graph(
    block_graph: blocks.BlockGraph,
    loader: jinja2.BaseLoader,
) -> str:
  """Generate the visualization webpage.

  Args:
    block_graph: blocks.BlockGraph. The block graph of the code.
    loader: A jinja22 loader

  Returns:
    str. The rendered visualization page.
  """
  return _generate_visualization(
      template_file=_BLOCKGRAPH_TEMPLATE_NAME,
      loader=loader,
      graph_data=block_serializer.encode_merged_graph(block_graph),
  )
