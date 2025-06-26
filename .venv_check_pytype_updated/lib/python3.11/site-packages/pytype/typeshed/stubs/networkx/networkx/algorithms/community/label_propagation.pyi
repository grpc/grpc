from _typeshed import Incomplete
from collections.abc import Generator

from networkx.utils.backends import _dispatch

@_dispatch
def asyn_lpa_communities(
    G, weight: Incomplete | None = None, seed: Incomplete | None = None
) -> Generator[Incomplete, Incomplete, None]: ...
@_dispatch
def label_propagation_communities(G): ...
