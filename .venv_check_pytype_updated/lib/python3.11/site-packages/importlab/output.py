from __future__ import print_function

import networkx as nx

from . import graph
from . import resolve


def inspect_graph(import_graph):
    keys = set(x[0] for x in import_graph.graph.edges)
    for key in sorted(keys):
        k = import_graph.format(key)
        for _, value in sorted(import_graph.graph.edges([key])):
            v = import_graph.format(value)
            print('  %s -> %s' % (k, v))
        for value in sorted(import_graph.broken_deps[key]):
            print('  %s -> <%s>' % (k, value))


def format_file_node(import_graph, node, indent):
    """Prettyprint nodes based on their provenance."""
    f = import_graph.provenance[node]
    if isinstance(f, resolve.Direct):
        out = '+ ' + f.short_path
    elif isinstance(f, resolve.Local):
        out = '  ' + f.short_path
    elif isinstance(f, resolve.System):
        out = ':: ' + f.short_path
    elif isinstance(f, resolve.Builtin):
        out = '(%s)' % f.module_name
    else:
        out = '%r' % node
    return '  '*indent + out


def format_node(import_graph, node, indent):
    """Helper function for print_tree"""
    if isinstance(node, graph.NodeSet):
        ind = '  ' * indent
        out = [ind + 'cycle {'] + [
                format_file_node(import_graph, n, indent + 1)
                for n in node.nodes
        ] + [ind + '}']
        return '\n'.join(out)
    else:
        return format_file_node(import_graph, node, indent)


def print_tree(import_graph):
    def _print_tree(root, indent=0):
        if root in seen:
            return
        seen.add(root)
        print(format_node(import_graph, root, indent))
        for _, v in import_graph.graph.out_edges([root]):
            _print_tree(v, indent=indent+2)

    seen = set()
    for root in nx.topological_sort(import_graph.graph):
        if not import_graph.graph.in_edges([root]):
            _print_tree(root)


def print_topological_sort(import_graph):
    for node in nx.topological_sort(import_graph.graph):
        print(import_graph.format(node))


def formatted_deps_list(import_graph):
    out = []
    for node, deps in import_graph.deps_list():
        out.append('source: ' + import_graph.format(node))
        if deps:
            out.append('deps:')
            for dep in deps:
                out.append('  ' + import_graph.format(dep))
    return '\n'.join(out)


def print_unresolved_dependencies(import_graph):
    for imp in sorted(import_graph.get_all_unresolved()):
        print(' ', imp.name)


def print_unreadable_files(import_graph):
    for f in sorted(import_graph.unreadable_files):
        print(' ', f)


def maybe_show_unreadable(import_graph):
    """Only print an unreadable files section if nonempty."""
    if import_graph.unreadable_files:
        print()
        print('Unreadable files:')
        print_unreadable_files(import_graph)
