# SPDX-FileCopyrightText: 2024 pydot contributors
#
# SPDX-License-Identifier: MIT

"""An interface to GraphViz."""

from __future__ import annotations

import copy
import errno
import itertools
import logging
import os
import re
import subprocess
import sys
import warnings
from typing import Any, Sequence, Union, cast

import pydot
from pydot._vendor import tempfile
from pydot.classes import AttributeDict, EdgeEndpoint, FrozenDict

_logger = logging.getLogger(__name__)
_logger.debug("pydot core module initializing")

# fmt: off
GRAPH_ATTRIBUTES = {
    "Damping", "K", "URL", "aspect", "bb", "bgcolor",
    "center", "charset", "clusterrank", "colorscheme", "comment", "compound",
    "concentrate", "defaultdist", "dim", "dimen", "diredgeconstraints",
    "dpi", "epsilon", "esep", "fontcolor", "fontname", "fontnames",
    "fontpath", "fontsize", "id", "label", "labeljust", "labelloc",
    "landscape", "layers", "layersep", "layout", "levels", "levelsgap",
    "lheight", "lp", "lwidth", "margin", "maxiter", "mclimit", "mindist",
    "mode", "model", "mosek", "nodesep", "nojustify", "normalize", "nslimit",
    "nslimit1", "ordering", "orientation", "outputorder", "overlap",
    "overlap_scaling", "pack", "packmode", "pad", "page", "pagedir",
    "quadtree", "quantum", "rankdir", "ranksep", "ratio", "remincross",
    "repulsiveforce", "resolution", "root", "rotate", "searchsize", "sep",
    "showboxes", "size", "smoothing", "sortv", "splines", "start",
    "stylesheet", "target", "truecolor", "viewport", "voro_margin",
    # for subgraphs
    "rank"
}


EDGE_ATTRIBUTES = {
    "URL", "arrowhead", "arrowsize", "arrowtail",
    "color", "colorscheme", "comment", "constraint", "decorate", "dir",
    "edgeURL", "edgehref", "edgetarget", "edgetooltip", "fontcolor",
    "fontname", "fontsize", "headURL", "headclip", "headhref", "headlabel",
    "headport", "headtarget", "headtooltip", "href", "id", "label",
    "labelURL", "labelangle", "labeldistance", "labelfloat", "labelfontcolor",
    "labelfontname", "labelfontsize", "labelhref", "labeltarget",
    "labeltooltip", "layer", "len", "lhead", "lp", "ltail", "minlen",
    "nojustify", "penwidth", "pos", "samehead", "sametail", "showboxes",
    "style", "tailURL", "tailclip", "tailhref", "taillabel", "tailport",
    "tailtarget", "tailtooltip", "target", "tooltip", "weight",
    "rank"
}


NODE_ATTRIBUTES = {
    "URL", "color", "colorscheme", "comment",
    "distortion", "fillcolor", "fixedsize", "fontcolor", "fontname",
    "fontsize", "group", "height", "id", "image", "imagescale", "label",
    "labelloc", "layer", "margin", "nojustify", "orientation", "penwidth",
    "peripheries", "pin", "pos", "rects", "regular", "root", "samplepoints",
    "shape", "shapefile", "showboxes", "sides", "skew", "sortv", "style",
    "target", "tooltip", "vertices", "width", "z",
    # The following are attributes dot2tex
    "texlbl",  "texmode"
}


CLUSTER_ATTRIBUTES = {
    "K", "URL", "bgcolor", "color", "colorscheme",
    "fillcolor", "fontcolor", "fontname", "fontsize", "label", "labeljust",
    "labelloc", "lheight", "lp", "lwidth", "nojustify", "pencolor",
    "penwidth", "peripheries", "sortv", "style", "target", "tooltip"
}
# fmt: on


OUTPUT_FORMATS = {
    "canon",
    "cmap",
    "cmapx",
    "cmapx_np",
    "dia",
    "dot",
    "fig",
    "gd",
    "gd2",
    "gif",
    "hpgl",
    "imap",
    "imap_np",
    "ismap",
    "jpe",
    "jpeg",
    "jpg",
    "mif",
    "mp",
    "pcl",
    "pdf",
    "pic",
    "plain",
    "plain-ext",
    "png",
    "ps",
    "ps2",
    "svg",
    "svgz",
    "vml",
    "vmlz",
    "vrml",
    "vtx",
    "wbmp",
    "xdot",
    "xlib",
}


DEFAULT_PROGRAMS = {
    "dot",
    "twopi",
    "neato",
    "circo",
    "fdp",
    "sfdp",
}


class frozendict(FrozenDict):
    """Deprecated alias for pydot.classes.FrozenDict."""

    def __init__(self, *args: Any, **kwargs: Any):
        warnings.warn(
            f"{self.__class__.__name__} is deprecated. "
            "Use pydot.classes.FrozenDict instead.",
            category=DeprecationWarning,
            stacklevel=2,
        )
        super().__init__(self, *args, **kwargs)


def __generate_attribute_methods(Klass: type[Common], attrs: set[str]) -> None:
    """Generate setter and getter methods for attributes."""
    for attr in attrs:
        # Generate all the Getter methods.
        #
        def __getter(self: Any, _attr: str = attr) -> Any:
            return self.get(_attr)

        setattr(Klass, f"get_{attr}", __getter)

        # Generate all the Setter methods.
        #
        def __setter(self: Any, *args: Any, _attr: str = attr) -> Any:
            return self.set(_attr, *args)

        setattr(Klass, f"set_{attr}", __setter)


def __generate_format_methods(Klass: type) -> None:
    """Generate create_ and write_ methods for formats."""
    # Automatically creates all
    # the methods enabling the creation
    # of output in any of the supported formats.
    for frmt in OUTPUT_FORMATS:

        def __create_method(
            self: Any,
            f: str = frmt,
            prog: str | None = None,
            encoding: str | None = None,
        ) -> Any:
            """Refer to docstring of method `create`."""
            return self.create(format=f, prog=prog, encoding=encoding)

        setattr(Klass, f"create_{frmt}", __create_method)

    for frmt in OUTPUT_FORMATS ^ {"raw"}:

        def __write_method(
            self: Any,
            path: str,
            f: str = frmt,
            prog: str | None = None,
            encoding: str | None = None,
        ) -> None:
            """Refer to docstring of method `write`."""
            self.write(path, format=f, prog=prog, encoding=encoding)

        setattr(Klass, f"write_{frmt}", __write_method)


def is_windows() -> bool:
    return os.name == "nt"


def is_anaconda() -> bool:
    import glob

    conda_pattern = os.path.join(sys.prefix, "conda-meta\\graphviz*.json")
    return glob.glob(conda_pattern) != []


def get_executable_extension() -> str:
    if is_windows():
        return ".bat" if is_anaconda() else ".exe"
    else:
        return ""


def call_graphviz(
    program: str,
    arguments: list[str],
    working_dir: str | bytes,
    **kwargs: Any,
) -> tuple[str, str, subprocess.Popen[str]]:
    # explicitly inherit `$PATH`, on Windows too,
    # with `shell=False`

    if program in DEFAULT_PROGRAMS:
        extension = get_executable_extension()
        program += extension

    if arguments is None:
        arguments = []

    if "creationflags" not in kwargs and hasattr(
        subprocess, "CREATE_NO_WINDOW"
    ):
        # Only on Windows OS:
        # specify that the new process shall not create a new window
        kwargs.update(creationflags=subprocess.CREATE_NO_WINDOW)

    env = {
        "PATH": os.environ.get("PATH", ""),
        "LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH", ""),
        "SYSTEMROOT": os.environ.get("SYSTEMROOT", ""),
    }

    program_with_args = [program] + arguments

    process = subprocess.Popen(
        program_with_args,
        env=env,
        cwd=working_dir,
        shell=False,
        stderr=subprocess.PIPE,
        stdout=subprocess.PIPE,
        **kwargs,
    )
    stdout_data, stderr_data = process.communicate()

    return stdout_data, stderr_data, process


def make_quoted(s: str) -> str:
    """Transform a string into a quoted string, escaping specials."""
    replace = {
        ord('"'): r"\"",
        ord("\n"): r"\n",
        ord("\r"): r"\r",
    }
    return rf'"{s.translate(replace)}"'


dot_keywords = ["graph", "subgraph", "digraph", "node", "edge", "strict"]

re_numeric = re.compile(r"^([0-9]+\.?[0-9]*|[0-9]*\.[0-9]+)$")
re_dbl_quoted = re.compile(r'^".*"$', re.S)
re_html = re.compile(r"^<.*>$", re.S)

id_re_alpha_nums = re.compile(r"^[_a-zA-Z][a-zA-Z0-9_]*$")
id_re_alpha_nums_with_ports = re.compile(
    r'^[_a-zA-Z][a-zA-Z0-9_:"]*[a-zA-Z0-9_"]+$'
)
id_re_with_port = re.compile(r"^([^:]*):([^:]*)$")


def any_needs_quotes(s: str) -> bool | None:
    """Determine if a string needs to be quoted.

    Returns True, False, or None if the result is indeterminate.
    """

    # Strings consisting _only_ of digits are safe unquoted
    if s.isdigit():
        return False

    # MIXED-aphanumeric values need quoting if they *start* with a digit
    if s.isalnum():
        return s[0].isdigit()

    has_high_chars = any(ord(c) > 0x7F or ord(c) == 0 for c in s)
    if has_high_chars and not re_dbl_quoted.match(s) and not re_html.match(s):
        return True

    for test_re in [re_numeric, re_dbl_quoted, re_html]:
        if test_re.match(s):
            return False

    return None


def id_needs_quotes(s: str) -> bool:
    """Checks whether a string is a dot language ID.

    It will check whether the string is solely composed
    by the characters allowed in an ID or not.
    If the string is one of the reserved keywords it will
    need quotes too but the user will need to add them
    manually.
    """

    # If the name is a reserved keyword it will need quotes but pydot
    # can't tell when it's being used as a keyword or when it's simply
    # a name. Hence the user needs to supply the quotes when an element
    # would use a reserved keyword as name. This function will return
    # false indicating that a keyword string, if provided as-is, won't
    # need quotes.
    if s.lower() in dot_keywords:
        return False

    any_result = any_needs_quotes(s)
    if any_result is not None:
        return any_result

    for test_re in [
        id_re_alpha_nums,
        id_re_alpha_nums_with_ports,
    ]:
        if test_re.match(s):
            return False

    m = id_re_with_port.match(s)
    if m:
        return id_needs_quotes(m.group(1)) or id_needs_quotes(m.group(2))

    return True


def quote_id_if_necessary(
    s: str, unquoted_keywords: Sequence[str] | None = None
) -> str:
    """Enclose identifier in quotes, if needed."""
    unquoted = [
        w.lower() for w in list(unquoted_keywords if unquoted_keywords else [])
    ]

    if isinstance(s, bool):
        return str(s).lower()
    if not isinstance(s, str):
        return s
    if not s:
        return s

    if s.lower() in unquoted:
        return s
    if s.lower() in dot_keywords:
        return make_quoted(s)

    if id_needs_quotes(s):
        return make_quoted(s)

    return s


def quote_attr_if_necessary(s: str) -> str:
    """Enclose attribute value in quotes, if needed."""
    if isinstance(s, bool):
        return str(s).lower()

    if not isinstance(s, str):
        return s

    if s.lower() in dot_keywords:
        return make_quoted(s)

    any_result = any_needs_quotes(s)
    if any_result is not None and not any_result:
        return s

    return make_quoted(s)


def graph_from_dot_data(s: str) -> list[Dot] | None:
    """Load graphs from DOT description in string `s`.

    This function is NOT thread-safe due to the internal use of `pyparsing`.
    Use a lock if needed.

    @param s: string in [DOT language](
        https://en.wikipedia.org/wiki/DOT_(graph_description_language))

    @return: Graphs that result from parsing.
    @rtype: `list` of `pydot.Dot`
    """
    import pydot.dot_parser

    return pydot.dot_parser.parse_dot_data(s)


def graph_from_dot_file(
    path: str | bytes, encoding: str | None = None
) -> list[Dot] | None:
    """Load graphs from DOT file at `path`.

    This function is NOT thread-safe due to the internal use of `pyparsing`.
    Use a lock if needed.

    @param path: to DOT file
    @param encoding: as passed to `io.open`.
        For example, `'utf-8'`.

    @return: Graphs that result from parsing.
    @rtype: `list` of `pydot.Dot`
    """
    with open(path, encoding=encoding) as f:
        s = f.read()
    graphs = graph_from_dot_data(s)
    return graphs


def graph_from_edges(
    edge_list: Sequence[Any], node_prefix: str = "", directed: bool = False
) -> Dot:
    """Creates a basic graph out of an edge list.

    The edge list has to be a list of tuples representing
    the nodes connected by the edge.
    The values can be anything: bool, int, float, str.

    If the graph is undirected by default, it is only
    calculated from one of the symmetric halves of the matrix.
    """

    if directed:
        graph = Dot(graph_type="digraph")

    else:
        graph = Dot(graph_type="graph")

    for edge in edge_list:
        if isinstance(edge[0], str):
            src = node_prefix + edge[0]
        else:
            src = node_prefix + str(edge[0])

        if isinstance(edge[1], str):
            dst = node_prefix + edge[1]
        else:
            dst = node_prefix + str(edge[1])

        e = Edge(src, dst)
        graph.add_edge(e)

    return graph


def graph_from_adjacency_matrix(
    matrix: Sequence[Sequence[Any]],
    node_prefix: str = "",
    directed: bool = False,
) -> Dot:
    """Creates a basic graph out of an adjacency matrix.

    The matrix has to be a list of rows of values
    representing an adjacency matrix.
    The values can be anything: bool, int, float, as long
    as they can evaluate to True or False.
    """

    node_orig = 1

    if directed:
        graph = Dot(graph_type="digraph")
    else:
        graph = Dot(graph_type="graph")

    for row in matrix:
        if not directed:
            skip = matrix.index(row)
            r = row[skip:]
        else:
            skip = 0
            r = row
        node_dest = skip + 1

        for e in r:
            if e:
                graph.add_edge(
                    Edge(
                        f"{node_prefix}{node_orig}",
                        f"{node_prefix}{node_dest}",
                    )
                )
            node_dest += 1
        node_orig += 1

    return graph


def graph_from_incidence_matrix(
    matrix: Sequence[Sequence[Any]],
    node_prefix: str = "",
    directed: bool = False,
) -> Dot:
    """Creates a basic graph out of an incidence matrix.

    The matrix has to be a list of rows of values
    representing an incidence matrix.
    The values can be anything: bool, int, float, as long
    as they can evaluate to True or False.
    """

    if directed:
        graph = Dot(graph_type="digraph")
    else:
        graph = Dot(graph_type="graph")

    for row in matrix:
        nodes = []
        c = 1

        for node in row:
            if node:
                nodes.append(c * node)
            c += 1
            nodes.sort()

        if len(nodes) == 2:
            graph.add_edge(
                Edge(
                    f"{node_prefix}{abs(nodes[0])}",
                    f"{node_prefix}{nodes[1]}",
                )
            )

    if not directed:
        graph.set_simplify(True)

    return graph


class Common:
    """Common information to several classes.

    Should not be directly used, several classes are derived from
    this one.
    """

    def __init__(self, obj_dict: AttributeDict | None = None) -> None:
        self.obj_dict: AttributeDict = obj_dict or {}

    def __getstate__(self) -> AttributeDict:
        _dict = copy.copy(self.obj_dict)
        return _dict

    def __setstate__(self, state: AttributeDict) -> None:
        self.obj_dict = state

    def set_parent_graph(self, parent_graph: Common | None) -> None:
        self.obj_dict["parent_graph"] = parent_graph

    def get_parent_graph(self) -> Graph | None:
        return self.obj_dict.get("parent_graph", None)

    def get_top_graph_type(self, default: str = "graph") -> str:
        """Find the topmost parent graph type for the current object."""
        parent = self.get_parent_graph()
        while parent is not None:
            parent_ = parent.get_parent_graph()
            if parent_ == parent:
                break
            parent = parent_
        if parent is None:
            return default
        return cast("str", parent.obj_dict.get("type", default))

    def set(self, name: str, value: Any) -> None:
        """Set an attribute value by name.

        Given an attribute 'name' it will set its value to 'value'.
        There's always the possibility of using the methods:

            set_'name'(value)

        which are defined for standard graphviz attributes.
        """
        self.obj_dict["attributes"][name] = value

    def get(self, name: str) -> Any:
        """Get an attribute value by name.

        Given an attribute 'name' it will get its value.
        There's always the possibility of using the methods:

            get_'name'()

        which are defined for standard graphviz attributes.
        """
        return self.obj_dict["attributes"].get(name, None)

    def get_attributes(self) -> AttributeDict:
        """Get attributes of the object"""
        return cast(AttributeDict, self.obj_dict.get("attributes", {}))

    def set_sequence(self, seq: int) -> None:
        """Set sequence"""
        self.obj_dict["sequence"] = seq

    def get_sequence(self) -> int | None:
        """Get sequence"""
        seq = self.obj_dict.get("sequence")
        if seq is None:
            return seq
        return int(seq)

    @staticmethod
    def get_indent(indent: Any, indent_level: int) -> str:
        if isinstance(indent, (int, float)):
            indent_str = " " * int(indent)
        else:
            indent_str = str(indent)
        return indent_str * indent_level

    @staticmethod
    def _format_attr(key: str, value: Any) -> str:
        """Turn a key-value pair into an attribute, properly quoted."""
        if value == "":
            value = '""'
        if value is not None:
            return f"{key}={quote_attr_if_necessary(value)}"
        return key

    def formatted_attr_list(self) -> list[str]:
        """Return a list of the class's attributes as formatted strings."""
        return [
            self._format_attr(k, v)
            for k, v in self.obj_dict["attributes"].items()
        ]

    def attrs_string(self, prefix: str = "") -> str:
        """Format the current attributes list for output.

        The `prefix` string will be prepended if and only if some
        output is generated."""
        attrs = self.formatted_attr_list()
        if not attrs:
            return ""
        return f"{prefix}[{', '.join(attrs)}]"


class Node(Common):
    """A graph node.

    This class represents a graph's node with all its attributes.

    node(name, attribute=value, ...)

    name: node's name

    All the attributes defined in the Graphviz dot language should
    be supported.
    """

    def __init__(
        self,
        name: str = "",
        obj_dict: AttributeDict | None = None,
        **attrs: Any,
    ) -> None:
        super().__init__(obj_dict)
        if obj_dict is None:
            # Copy the attributes
            #
            self.obj_dict["attributes"] = dict(attrs)
            self.obj_dict["type"] = "node"
            self.obj_dict["parent_graph"] = None
            self.obj_dict["sequence"] = None

            # Remove the compass point
            #
            port = None
            if isinstance(name, str) and not name.startswith('"'):
                idx = name.find(":")
                if idx > 0 and idx + 1 < len(name):
                    name, port = name[:idx], name[idx:]

            if isinstance(name, int):
                name = str(name)

            self.obj_dict["name"] = name
            self.obj_dict["port"] = port

    def __str__(self) -> str:
        return self.to_string()

    def set_name(self, node_name: str | None) -> None:
        """Set the node's name."""
        self.obj_dict["name"] = node_name

    def get_name(self) -> str:
        """Get the node's name."""
        return self.obj_dict["name"]  # type: ignore

    def get_port(self) -> str | None:
        """Get the node's port."""
        return self.obj_dict["port"]  # type: ignore

    def add_style(self, style: str) -> None:
        styles = self.obj_dict["attributes"].get("style", None)
        if not styles and style:
            styles = [style]
        else:
            styles = styles.split(",")
            styles.append(style)

        self.obj_dict["attributes"]["style"] = ",".join(styles)

    def to_string(self, indent: Any = "", indent_level: int = 1) -> str:
        """Return string representation of node in DOT language."""
        indent_str = self.get_indent(indent, indent_level)

        node = quote_id_if_necessary(
            self.obj_dict["name"], unquoted_keywords=("graph", "node", "edge")
        )

        # No point in having default nodes that don't set any attributes...
        if (
            node in ("graph", "node", "edge")
            and len(self.obj_dict.get("attributes", {})) == 0
        ):
            return ""

        return f"{indent_str}{node}{self.attrs_string(prefix=' ')};"


__generate_attribute_methods(Node, NODE_ATTRIBUTES)


class Edge(Common):
    """A graph edge.

    This class represents a graph's edge with all its attributes.

    edge(src, dst, attribute=value, ...)

    src: source node, subgraph or cluster
    dst: destination node, subgraph or cluster

    `src` and `dst` can be specified as a `Node`, `Subgraph` or
    `Cluster` object, or as the name string of such a component.

    All the attributes defined in the Graphviz dot language should
    be supported.

    Attributes can be set through the dynamically generated methods:

     set_[attribute name], i.e. set_label, set_fontname

    or directly by using the instance's special dictionary:

     Edge.obj_dict['attributes'][attribute name], i.e.

        edge_instance.obj_dict['attributes']['label']
        edge_instance.obj_dict['attributes']['fontname']

    """

    def __init__(
        self,
        src: EdgeDefinition | Sequence[EdgeDefinition] = "",
        dst: EdgeDefinition = "",
        obj_dict: AttributeDict | None = None,
        **attrs: Any,
    ) -> None:
        super().__init__(obj_dict)
        if obj_dict is None:
            if isinstance(src, (list, tuple)):
                _src, _dst = src[0:2]
            else:
                _src, _dst = src, dst

            ep0: EdgeEndpoint
            ep1: EdgeEndpoint

            if isinstance(_src, (Node, Subgraph, Cluster)):
                ep0 = str(_src.get_name())
            elif isinstance(_src, (FrozenDict, int, float)):
                ep0 = _src
            else:
                ep0 = str(_src)

            if isinstance(_dst, (Node, Subgraph, Cluster)):
                ep1 = str(_dst.get_name())
            elif isinstance(_dst, (FrozenDict, int, float)):
                ep1 = _dst
            else:
                ep1 = str(_dst)

            points = (ep0, ep1)

            self.obj_dict["points"] = points
            self.obj_dict["attributes"] = dict(attrs)
            self.obj_dict["type"] = "edge"
            self.obj_dict["parent_graph"] = None
            self.obj_dict["sequence"] = None

    def __str__(self) -> str:
        return self.to_string()

    def _get_endpoint(self, position: int) -> EdgeEndpoint:
        ep = self.obj_dict["points"][position]
        if isinstance(ep, (FrozenDict, int, float)):
            return ep
        return str(ep)

    def get_source(self) -> EdgeEndpoint:
        """Get the edge's source endpoint."""
        return self._get_endpoint(0)

    def get_destination(self) -> EdgeEndpoint:
        """Get the edge's destination endpoint."""
        return self._get_endpoint(1)

    def __hash__(self) -> int:
        return hash(hash(self.get_source()) + hash(self.get_destination()))

    def __eq__(self, edge: object) -> bool:
        """Compare two edges.

        If the parent graph is directed, arcs linking
        node A to B are considered equal and A->B != B->A

        If the parent graph is undirected, any edge
        connecting two nodes is equal to any other
        edge connecting the same nodes, A->B == B->A
        """

        if not isinstance(edge, Edge):
            raise pydot.Error("Can not compare an edge to a non-edge object.")

        if self.get_top_graph_type() == "graph":
            # If the graph is undirected, the edge has neither
            # source nor destination.
            #
            if (
                self.get_source() == edge.get_source()
                and self.get_destination() == edge.get_destination()
            ) or (
                edge.get_source() == self.get_destination()
                and edge.get_destination() == self.get_source()
            ):
                return True

        else:
            if (
                self.get_source() == edge.get_source()
                and self.get_destination() == edge.get_destination()
            ):
                return True

        return False

    def parse_node_ref(self, node_ref: EdgeEndpoint) -> EdgeEndpoint:
        if not isinstance(node_ref, str):
            return node_ref

        if node_ref.startswith('"') and node_ref.endswith('"'):
            return node_ref

        node_port_idx = node_ref.rfind(":")

        if (
            node_port_idx > 0
            and node_ref[0] == '"'
            and node_ref[node_port_idx - 1] == '"'
        ):
            return node_ref

        if node_port_idx > 0:
            a = node_ref[:node_port_idx]
            b = node_ref[node_port_idx + 1 :]

            node = quote_id_if_necessary(a)
            node += ":" + quote_id_if_necessary(b)

            return node

        return quote_id_if_necessary(node_ref)

    def to_string(self, indent: Any = "", indent_level: int = 1) -> str:
        """Return string representation of edge in DOT language."""
        src = self.parse_node_ref(self.get_source())
        dst = self.parse_node_ref(self.get_destination())

        indent_str = self.get_indent(indent, indent_level)

        if isinstance(src, FrozenDict):
            sgraph = Subgraph(obj_dict=src)
            edge = [
                sgraph.to_string(
                    indent=indent, indent_level=indent_level, inline=True
                )
            ]
        else:
            edge = [str(src)]

        if self.get_top_graph_type() == "digraph":
            edge.append("->")
        else:
            edge.append("--")

        if isinstance(dst, FrozenDict):
            sgraph = Subgraph(obj_dict=dst)
            edge.append(
                sgraph.to_string(
                    indent=indent, indent_level=indent_level, inline=True
                )
            )
        else:
            edge.append(str(dst))

        return f"{indent_str}{' '.join(edge)}{self.attrs_string(prefix=' ')};"


__generate_attribute_methods(Edge, EDGE_ATTRIBUTES)


class Graph(Common):
    """Class representing a graph in Graphviz's dot language.

    This class implements the methods to work on a representation
    of a graph in Graphviz's dot language.

    graph(  graph_name='G', graph_type='digraph',
        strict=False, suppress_disconnected=False, attribute=value, ...)

    graph_name:
        the graph's name
    graph_type:
        can be 'graph' or 'digraph'
    suppress_disconnected:
        defaults to False, which will remove from the
        graph any disconnected nodes.
    simplify:
        if True it will avoid displaying equal edges, i.e.
        only one edge between two nodes. removing the
        duplicated ones.

    All the attributes defined in the Graphviz dot language should
    be supported.

    Attributes can be set through the dynamically generated methods:

     set_[attribute name], i.e. set_size, set_fontname

    or using the instance's attributes:

     Graph.obj_dict['attributes'][attribute name], i.e.

        graph_instance.obj_dict['attributes']['label']
        graph_instance.obj_dict['attributes']['fontname']
    """

    def __init__(
        self,
        graph_name: str = "G",
        obj_dict: AttributeDict | None = None,
        graph_type: str = "digraph",
        strict: bool = False,
        suppress_disconnected: bool = False,
        simplify: bool = False,
        **attrs: Any,
    ) -> None:
        super().__init__(obj_dict)
        if obj_dict is None:
            self.obj_dict["attributes"] = dict(attrs)

            if graph_type not in ["graph", "digraph"]:
                raise pydot.Error(
                    f'Invalid type "{graph_type}". '
                    "Accepted graph types are: graph, digraph"
                )

            self.obj_dict["name"] = graph_name
            self.obj_dict["type"] = graph_type

            self.obj_dict["strict"] = strict
            self.obj_dict["suppress_disconnected"] = suppress_disconnected
            self.obj_dict["simplify"] = simplify

            self.obj_dict["current_child_sequence"] = 1
            self.obj_dict["nodes"] = {}
            self.obj_dict["edges"] = {}
            self.obj_dict["subgraphs"] = {}

            self.set_parent_graph(self)

    def __str__(self) -> str:
        return self.to_string()

    def get_graph_type(self) -> str | None:
        return self.obj_dict["type"]  # type: ignore

    def set_graph_defaults(self, **attrs: Any) -> None:
        self.add_node(Node("graph", **attrs))

    def get_graph_defaults(self) -> Any:
        graph_nodes = self.get_node("graph")
        return [node.get_attributes() for node in graph_nodes]

    def set_node_defaults(self, **attrs: Any) -> None:
        """Define default node attributes.

        These attributes only apply to nodes added to the graph after
        calling this method.
        """
        self.add_node(Node("node", **attrs))

    def get_node_defaults(self) -> Any:
        graph_nodes = self.get_node("node")
        return [node.get_attributes() for node in graph_nodes]

    def set_edge_defaults(self, **attrs: Any) -> None:
        self.add_node(Node("edge", **attrs))

    def get_edge_defaults(self) -> Any:
        graph_nodes = self.get_node("edge")
        return [node.get_attributes() for node in graph_nodes]

    def set_simplify(self, simplify: bool) -> None:
        """Set whether to simplify or not.

        If True it will avoid displaying equal edges, i.e.
        only one edge between two nodes. removing the
        duplicated ones.
        """
        self.obj_dict["simplify"] = simplify

    def get_simplify(self) -> bool:
        """Get whether to simplify or not.

        Refer to set_simplify for more information.
        """
        return bool(self.obj_dict.get("simplify", False))

    def set_type(self, graph_type: str) -> None:
        """Set the graph's type, 'graph' or 'digraph'."""
        self.obj_dict["type"] = graph_type

    def get_type(self) -> str | None:
        """Get the graph's type, 'graph' or 'digraph'."""
        return self.obj_dict["type"]  # type: ignore

    def set_name(self, graph_name: str) -> None:
        """Set the graph's name."""
        self.obj_dict["name"] = graph_name

    def get_name(self) -> str | None:
        """Get the graph's name."""
        return self.obj_dict["name"]  # type: ignore

    def set_strict(self, val: bool) -> None:
        """Set graph to 'strict' mode.

        This option is only valid for top level graphs.
        """
        self.obj_dict["strict"] = val

    def get_strict(self) -> bool:
        """Get graph's 'strict' mode (True, False).

        This option is only valid for top level graphs.
        """
        return bool(self.obj_dict.get("strict", False))

    def set_suppress_disconnected(self, val: bool) -> None:
        """Suppress disconnected nodes in the output graph.

        This option will skip nodes in
        the graph with no incoming or outgoing
        edges. This option works also
        for subgraphs and has effect only in the
        current graph/subgraph.
        """
        self.obj_dict["suppress_disconnected"] = val

    def get_suppress_disconnected(self) -> bool:
        """Get if suppress disconnected is set.

        Refer to set_suppress_disconnected for more information.
        """
        return bool(self.obj_dict.get("suppress_disconnected", False))

    def get_next_sequence_number(self) -> int:
        seq: int = self.obj_dict.get("current_child_sequence", 1)
        self.obj_dict["current_child_sequence"] = seq + 1
        return seq

    def add_node(self, graph_node: Node) -> None:
        """Adds a node object to the graph.

        It takes a node object as its only argument and returns
        None.
        """
        if not isinstance(graph_node, Node):
            raise TypeError(
                "add_node() received "
                + "a non node class object: "
                + str(graph_node)
            )

        node = self.get_node(graph_node.get_name())

        if not node:
            self.obj_dict["nodes"][graph_node.get_name()] = [
                graph_node.obj_dict
            ]
        else:
            self.obj_dict["nodes"][graph_node.get_name()].append(
                graph_node.obj_dict
            )

        if not node or graph_node.get_parent_graph() is None:
            graph_node.set_parent_graph(self.get_parent_graph())

        graph_node.set_sequence(self.get_next_sequence_number())

    def del_node(self, name: str | Node, index: int | None = None) -> bool:
        """Delete a node from the graph.

        Given a node's name all node(s) with that same name
        will be deleted if 'index' is not specified or set
        to None.
        If there are several nodes with that same name and
        'index' is given, only the node in that position
        will be deleted.

        'index' should be an integer specifying the position
        of the node to delete. If index is larger than the
        number of nodes with that name, no action is taken.

        If nodes are deleted it returns True. If no action
        is taken it returns False.
        """
        if isinstance(name, Node):
            name = name.get_name()

        if name in self.obj_dict["nodes"]:
            if index is not None and index < len(self.obj_dict["nodes"][name]):
                del self.obj_dict["nodes"][name][index]
                return True
            else:
                del self.obj_dict["nodes"][name]
                return True

        return False

    def get_node(self, name: str) -> list[Node]:
        """Retrieve a node from the graph.

        Given a node's name the corresponding Node
        instance will be returned.

        If one or more nodes exist with that name a list of
        Node instances is returned.
        An empty list is returned otherwise.
        """
        match = []

        if name in self.obj_dict["nodes"]:
            match.extend(
                [
                    Node(obj_dict=obj_dict)
                    for obj_dict in self.obj_dict["nodes"][name]
                ]
            )

        return match

    def get_nodes(self) -> list[Node]:
        """Get the list of Node instances."""
        return self.get_node_list()

    def get_node_list(self) -> list[Node]:
        """Get the list of Node instances.

        This method returns the list of Node instances
        composing the graph.
        """
        node_objs: list[Node] = []

        for node in self.obj_dict["nodes"]:
            obj_dict_list = self.obj_dict["nodes"][node]
            node_objs.extend([Node(obj_dict=obj_d) for obj_d in obj_dict_list])

        return node_objs

    def add_edge(self, graph_edge: Edge) -> None:
        """Adds an edge object to the graph.

        It takes a edge object as its only argument and returns
        None.
        """
        if not isinstance(graph_edge, Edge):
            raise TypeError(
                "add_edge() received a non edge class object: "
                + str(graph_edge)
            )

        edge_points = (graph_edge.get_source(), graph_edge.get_destination())

        if edge_points in self.obj_dict["edges"]:
            edge_list = self.obj_dict["edges"][edge_points]
            edge_list.append(graph_edge.obj_dict)
        else:
            self.obj_dict["edges"][edge_points] = [graph_edge.obj_dict]

        graph_edge.set_sequence(self.get_next_sequence_number())
        graph_edge.set_parent_graph(self.get_parent_graph())

    def del_edge(
        self, src_or_list: Any, dst: Any = None, index: int | None = None
    ) -> bool:
        """Delete an edge from the graph.

        Given an edge's (source, destination) node names all
        matching edges(s) will be deleted if 'index' is not
        specified or set to None.
        If there are several matching edges and 'index' is
        given, only the edge in that position will be deleted.

        'index' should be an integer specifying the position
        of the edge to delete. If index is larger than the
        number of matching edges, no action is taken.

        If edges are deleted it returns True. If no action
        is taken it returns False.
        """
        if isinstance(src_or_list, (list, tuple)):
            if dst is not None and isinstance(dst, int):
                index = dst
            src, dst = src_or_list
        else:
            src, dst = src_or_list, dst

        if isinstance(src, Node):
            src = src.get_name()

        if isinstance(dst, Node):
            dst = dst.get_name()

        if (src, dst) in self.obj_dict["edges"]:
            if index is not None and index < len(
                self.obj_dict["edges"][(src, dst)]
            ):
                del self.obj_dict["edges"][(src, dst)][index]
                return True
            else:
                del self.obj_dict["edges"][(src, dst)]
                return True

        return False

    def get_edge(self, src_or_list: Any, dst: Any = None) -> list[Edge]:
        """Retrieved an edge from the graph.

        Given an edge's source and destination the corresponding
        Edge instance(s) will be returned.

        If one or more edges exist with that source and destination
        a list of Edge instances is returned.
        An empty list is returned otherwise.
        """
        if isinstance(src_or_list, (list, tuple)) and dst is None:
            edge_points = tuple(src_or_list)
            edge_points_reverse = (edge_points[1], edge_points[0])
        else:
            edge_points = (src_or_list, dst)
            edge_points_reverse = (dst, src_or_list)

        match = []

        if edge_points in self.obj_dict["edges"] or (
            self.get_top_graph_type() == "graph"
            and edge_points_reverse in self.obj_dict["edges"]
        ):
            edges_obj_dict = self.obj_dict["edges"].get(
                edge_points,
                self.obj_dict["edges"].get(edge_points_reverse, None),
            )

            for edge_obj_dict in edges_obj_dict:
                match.append(
                    Edge(
                        edge_points[0], edge_points[1], obj_dict=edge_obj_dict
                    )
                )

        return match

    def get_edges(self) -> list[Edge]:
        return self.get_edge_list()

    def get_edge_list(self) -> list[Edge]:
        """Get the list of Edge instances.

        This method returns the list of Edge instances
        composing the graph.
        """
        edge_objs = []

        for edge in self.obj_dict["edges"]:
            obj_dict_list = self.obj_dict["edges"][edge]
            edge_objs.extend([Edge(obj_dict=obj_d) for obj_d in obj_dict_list])

        return edge_objs

    def add_subgraph(self, sgraph: Subgraph) -> None:
        """Adds an subgraph object to the graph.

        It takes a subgraph object as its only argument and returns
        None.
        """
        if not isinstance(sgraph, Subgraph) and not isinstance(
            sgraph, Cluster
        ):
            raise TypeError(
                "add_subgraph() received a non subgraph class object:"
                + str(sgraph)
            )

        if sgraph.get_name() in self.obj_dict["subgraphs"]:
            sgraph_list = self.obj_dict["subgraphs"][sgraph.get_name()]
            sgraph_list.append(sgraph.obj_dict)

        else:
            self.obj_dict["subgraphs"][sgraph.get_name()] = [sgraph.obj_dict]

        sgraph.set_sequence(self.get_next_sequence_number())
        sgraph.set_parent_graph(self.get_parent_graph())

    def get_subgraph(self, name: str) -> list[Subgraph]:
        """Retrieved a subgraph from the graph.

        Given a subgraph's name the corresponding
        Subgraph instance will be returned.

        If one or more subgraphs exist with the same name, a list of
        Subgraph instances is returned.
        An empty list is returned otherwise.
        """
        match = []

        if name in self.obj_dict["subgraphs"]:
            sgraphs_obj_dict = self.obj_dict["subgraphs"].get(name)

            for obj_dict_list in sgraphs_obj_dict:
                match.append(Subgraph(obj_dict=obj_dict_list))

        return match

    def get_subgraphs(self) -> list[Subgraph]:
        return self.get_subgraph_list()

    def get_subgraph_list(self) -> list[Subgraph]:
        """Get the list of Subgraph instances.

        This method returns the list of Subgraph instances
        in the graph.
        """
        sgraph_objs = []

        for sgraph in self.obj_dict["subgraphs"]:
            obj_dict_list = self.obj_dict["subgraphs"][sgraph]
            sgraph_objs.extend(
                [Subgraph(obj_dict=obj_d) for obj_d in obj_dict_list]
            )

        return sgraph_objs

    def set_parent_graph(self, parent_graph: Common | None) -> None:
        self.obj_dict["parent_graph"] = parent_graph

        for k in self.obj_dict["nodes"]:
            obj_list = self.obj_dict["nodes"][k]
            for obj in obj_list:
                obj["parent_graph"] = parent_graph

        for k in self.obj_dict["edges"]:
            obj_list = self.obj_dict["edges"][k]
            for obj in obj_list:
                obj["parent_graph"] = parent_graph

        for k in self.obj_dict["subgraphs"]:
            obj_list = self.obj_dict["subgraphs"][k]
            for obj in obj_list:
                Graph(obj_dict=obj).set_parent_graph(parent_graph)

    def to_string(
        self, indent: Any = "", indent_level: int = 0, inline: bool = False
    ) -> str:
        """Return string representation of graph in DOT language.

        @return: graph and subelements
        @rtype: `str`
        """
        indent_str = self.get_indent(indent, indent_level)
        child_indent = self.get_indent(indent, indent_level + 1)

        graph = []

        if not inline:
            graph.append(indent_str)

        first_line = []

        if self == self.get_parent_graph() and self.get_strict():
            first_line.append("strict")

        graph_type = self.obj_dict["type"]
        if graph_type != "subgraph" or self.obj_dict.get("show_keyword", True):
            first_line.append(graph_type)

            # Suppressing the keyword hides the name as well
            graph_name = self.obj_dict.get("name")
            if graph_name:
                first_line.append(quote_id_if_necessary(graph_name))

        first_line.append("{\n")
        graph.append(" ".join(first_line))

        graph.extend(
            f"{child_indent}{a};\n" for a in self.formatted_attr_list()
        )

        edges_done = set()

        edge_obj_dicts = []
        for k in self.obj_dict["edges"]:
            edge_obj_dicts.extend(self.obj_dict["edges"][k])

        if edge_obj_dicts:
            edge_ep_set = set(
                itertools.chain.from_iterable(
                    obj["points"] for obj in edge_obj_dicts
                )
            )
        else:
            edge_ep_set = set()

        node_obj_dicts = []
        for k in self.obj_dict["nodes"]:
            node_obj_dicts.extend(self.obj_dict["nodes"][k])

        sgraph_obj_dicts = []
        for k in self.obj_dict["subgraphs"]:
            sgraph_obj_dicts.extend(self.obj_dict["subgraphs"][k])

        obj_list = [
            (obj["sequence"], obj)
            for obj in (edge_obj_dicts + node_obj_dicts + sgraph_obj_dicts)
        ]
        obj_list.sort(key=lambda x: x[0])

        skip_disconnected = self.get_suppress_disconnected()
        simplify = self.get_simplify()

        for idx, obj in obj_list:
            if obj["type"] == "node":
                node = Node(obj_dict=obj)

                if skip_disconnected and node.get_name() not in edge_ep_set:
                    continue

                node_str = node.to_string(
                    indent=indent, indent_level=indent_level + 1
                )
                graph.append(f"{node_str}\n")

            elif obj["type"] == "edge":
                edge = Edge(obj_dict=obj)

                if simplify and edge in edges_done:
                    continue

                edge_str = edge.to_string(
                    indent=indent, indent_level=indent_level + 1
                )
                graph.append(f"{edge_str}\n")
                edges_done.add(edge)

            else:
                sgraph_str = Subgraph(obj_dict=obj).to_string(
                    indent=indent, indent_level=indent_level + 1
                )
                graph.append(f"{sgraph_str}")  # No newline, already present

        graph.append(f"{indent_str}}}")
        if not inline:
            graph.append("\n")

        return "".join(graph)


__generate_attribute_methods(Graph, GRAPH_ATTRIBUTES)


class Subgraph(Graph):
    """Class representing a subgraph in Graphviz's dot language.

    This class implements the methods to work on a representation
    of a subgraph in Graphviz's dot language.

    subgraph(graph_name='subG',
             suppress_disconnected=False,
             attribute=value,
             ...)

    graph_name:
        the subgraph's name
    suppress_disconnected:
        defaults to false, which will remove from the
        subgraph any disconnected nodes.
    All the attributes defined in the Graphviz dot language should
    be supported.

    Attributes can be set through the dynamically generated methods:

     set_[attribute name], i.e. set_size, set_fontname

    or using the instance's attributes:

     Subgraph.obj_dict['attributes'][attribute name], i.e.

        subgraph_instance.obj_dict['attributes']['label']
        subgraph_instance.obj_dict['attributes']['fontname']
    """

    # RMF: subgraph should have all the
    # attributes of graph so it can be passed
    # as a graph to all methods
    #
    def __init__(
        self,
        graph_name: str = "",
        obj_dict: AttributeDict | None = None,
        suppress_disconnected: bool = False,
        simplify: bool = False,
        **attrs: Any,
    ) -> None:
        super().__init__(
            graph_name=graph_name,
            obj_dict=obj_dict,
            suppress_disconnected=suppress_disconnected,
            simplify=simplify,
            **attrs,
        )
        if obj_dict is None:
            self.obj_dict["type"] = "subgraph"


class Cluster(Graph):
    """Class representing a cluster in Graphviz's dot language.

    This class implements the methods to work on a representation
    of a cluster in Graphviz's dot language.

    cluster(graph_name='subG',
            suppress_disconnected=False,
            attribute=value,
            ...)

    graph_name:
        the cluster's name
        (the string 'cluster' will be always prepended)
    suppress_disconnected:
        defaults to false, which will remove from the
        cluster any disconnected nodes.
    All the attributes defined in the Graphviz dot language should
    be supported.

    Attributes can be set through the dynamically generated methods:

     set_[attribute name], i.e. set_color, set_fontname

    or using the instance's attributes:

     Cluster.obj_dict['attributes'][attribute name], i.e.

        cluster_instance.obj_dict['attributes']['label']
        cluster_instance.obj_dict['attributes']['fontname']
    """

    def __init__(
        self,
        graph_name: str = "subG",
        obj_dict: AttributeDict | None = None,
        suppress_disconnected: bool = False,
        simplify: bool = False,
        **attrs: Any,
    ) -> None:
        super().__init__(
            graph_name=graph_name,
            obj_dict=obj_dict,
            suppress_disconnected=suppress_disconnected,
            simplify=simplify,
            **attrs,
        )
        if obj_dict is None:
            self.obj_dict["type"] = "subgraph"
            self.obj_dict["name"] = quote_id_if_necessary(
                "cluster_" + graph_name
            )


__generate_attribute_methods(Cluster, CLUSTER_ATTRIBUTES)


class Dot(Graph):
    """A container for handling a dot language file.

    This class implements methods to write and process
    a dot language file. It is a derived class of
    the base class 'Graph'.
    """

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        super().__init__(*args, **kwargs)

        self.shape_files: list[str] = []
        self.formats = OUTPUT_FORMATS
        self.prog = "dot"

    def __getstate__(self) -> AttributeDict:
        state = {
            "obj_dict": copy.copy(self.obj_dict),
            "prog": self.prog,
            "shape_files": copy.deepcopy(self.shape_files),
            "formats": copy.copy(self.formats),
        }
        return state

    def __setstate__(self, state: AttributeDict) -> None:
        if "obj_dict" not in state:
            # Backwards compatibility for old picklings
            state = {"obj_dict": state}
        self.obj_dict = state.get("obj_dict", {})
        self.prog = state.get("prog", "dot")
        self.shape_files = state.get("shape_files", [])
        self.formats = state.get("formats", OUTPUT_FORMATS)

    def set_shape_files(self, file_paths: str | Sequence[str]) -> None:
        """Add the paths of the required image files.

        If the graph needs graphic objects to
        be used as shapes or otherwise
        those need to be in the same folder as
        the graph is going to be rendered
        from. Alternatively the absolute path to
        the files can be specified when
        including the graphics in the graph.

        The files in the location pointed to by
        the path(s) specified as arguments
        to this method will be copied to
        the same temporary location where the
        graph is going to be rendered.
        """
        if isinstance(file_paths, str):
            self.shape_files.append(file_paths)

        if isinstance(file_paths, (list, tuple)):
            self.shape_files.extend(file_paths)

    def set_prog(self, prog: str) -> None:
        """Sets the default program.

        Sets the default program in charge of processing
        the dot file into a graph.
        """
        self.prog = prog

    def write(
        self,
        path: str | bytes,
        prog: str | None = None,
        format: str = "raw",
        encoding: str | None = None,
    ) -> bool:
        """Writes a graph to a file.

        Given a filename 'path' it will open/create and truncate
        such file and write on it a representation of the graph
        defined by the dot object in the format specified by
        'format' and using the encoding specified by `encoding` for text.
        The format 'raw' is used to dump the string representation
        of the Dot object, without further processing.
        The output can be processed by any of graphviz tools, defined
        in 'prog', which defaults to 'dot'
        Returns True or False according to the success of the write
        operation.

        There's also the preferred possibility of using:

            write_'format'(path, prog='program')

        which are automatically defined for all the supported formats.
        [write_ps(), write_gif(), write_dia(), ...]

        The encoding is passed to `open` [1].

        [1] https://docs.python.org/3/library/functions.html#open
        """
        if prog is None:
            prog = self.prog
        if format == "raw":
            s = self.to_string()
            with open(path, mode="w", encoding=encoding) as f:
                f.write(s)
        else:
            s = self.create(prog, format, encoding=encoding)
            with open(path, mode="wb") as f:
                f.write(s)  # type: ignore
        return True

    def create(
        self,
        prog: list[str] | tuple[str] | str | None = None,
        format: str = "ps",
        encoding: str | None = None,
    ) -> str:
        """Creates and returns a binary image for the graph.

        create will write the graph to a tempworary dot file in the
        encoding specified by `encoding` and process it with the
        program given by 'prog' (which defaults to 'twopi'), reading
        the binary image output and return it as `bytes`.

        There's also the preferred possibility of using:

            create_'format'(prog='program')

        which are automatically defined for all the supported formats,
        for example:

          - `create_ps()`
          - `create_gif()`
          - `create_dia()`

        If 'prog' is a list, instead of a string,
        then the fist item is expected to be the program name,
        followed by any optional command-line arguments for it:

            [ 'twopi', '-Tdot', '-s10' ]


        @param prog: either:

          - name of GraphViz executable that
            can be found in the `$PATH`, or

          - absolute path to GraphViz executable.

          If you have added GraphViz to the `$PATH` and
          use its executables as installed
          (without renaming any of them)
          then their names are:

            - `'dot'`
            - `'twopi'`
            - `'neato'`
            - `'circo'`
            - `'fdp'`
            - `'sfdp'`

          On Windows, these have the notorious ".exe" extension that,
          only for the above strings, will be added automatically.

          The `$PATH` is inherited from `os.env['PATH']` and
          passed to `subprocess.Popen` using the `env` argument.

          If you haven't added GraphViz to your `$PATH` on Windows,
          then you may want to give the absolute path to the
          executable (for example, to `dot.exe`) in `prog`.
        """
        if prog is None:
            prog = self.prog

        assert prog is not None

        if isinstance(prog, (list, tuple)):
            prog, args = prog[0], prog[1:]
        else:
            args = []  # type: ignore

        # temp file
        with tempfile.TemporaryDirectory(
            ignore_cleanup_errors=True
        ) as tmp_dir:  # type: ignore
            fp = tempfile.NamedTemporaryFile(dir=tmp_dir, delete=False)
            fp.close()
            self.write(fp.name, encoding=encoding)

            # For each of the image files, copy it to the temporary directory
            # with the same filename as the original
            for img in self.shape_files:
                outfile = os.path.join(tmp_dir, os.path.basename(img))
                with open(img, "rb") as img_in, open(outfile, "wb") as img_out:
                    img_data = img_in.read()
                    img_out.write(img_data)

            arguments = [f"-T{format}"] + args + [fp.name]  # type: ignore

            try:
                stdout_data, stderr_data, process = call_graphviz(
                    program=prog,
                    arguments=arguments,
                    working_dir=tmp_dir,
                )
            except OSError as e:
                if e.errno == errno.ENOENT:
                    args = list(e.args)  # type: ignore
                    args[1] = f'"{prog}" not found in path.'  # type: ignore
                    raise OSError(*args)
                else:
                    raise

        if process.returncode != 0:
            code = process.returncode
            print(
                f'"{prog}" with args {arguments} returned code: {code}\n\n'
                f"stdout, stderr:\n {stdout_data}\n{stderr_data}\n"
            )

        assert process.returncode == 0, (
            f'"{prog}" with args {arguments} '
            f"returned code: {process.returncode}"
        )

        return stdout_data


__generate_format_methods(Dot)


# Type alias for forward-referenced type
EdgeDefinition = Union[EdgeEndpoint, Node, Subgraph, Cluster]
