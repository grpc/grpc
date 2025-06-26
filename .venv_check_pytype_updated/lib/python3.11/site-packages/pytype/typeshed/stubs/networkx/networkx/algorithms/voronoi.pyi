from networkx.utils.backends import _dispatch

@_dispatch
def voronoi_cells(G, center_nodes, weight: str = "weight"): ...
