from networkx.utils.backends import _dispatch

@_dispatch
def k_components(G, min_density: float = 0.95): ...
