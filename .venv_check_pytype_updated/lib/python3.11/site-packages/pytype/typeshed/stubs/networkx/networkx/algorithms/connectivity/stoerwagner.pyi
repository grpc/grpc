from networkx.utils.backends import _dispatch

@_dispatch
def stoer_wagner(G, weight: str = "weight", heap=...): ...
