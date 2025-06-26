from networkx.utils.backends import _dispatch

@_dispatch
def stochastic_graph(G, copy: bool = True, weight: str = "weight"): ...
