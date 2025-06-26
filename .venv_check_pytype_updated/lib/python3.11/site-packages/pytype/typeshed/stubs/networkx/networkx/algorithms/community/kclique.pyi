from _typeshed import Incomplete
from collections.abc import Generator

from networkx.utils.backends import _dispatch

@_dispatch
def k_clique_communities(G, k, cliques: Incomplete | None = None) -> Generator[Incomplete, None, None]: ...
