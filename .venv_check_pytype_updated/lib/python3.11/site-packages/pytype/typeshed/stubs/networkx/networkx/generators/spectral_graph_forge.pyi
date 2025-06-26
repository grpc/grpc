from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def spectral_graph_forge(G, alpha, transformation: str = "identity", seed: Incomplete | None = None): ...
