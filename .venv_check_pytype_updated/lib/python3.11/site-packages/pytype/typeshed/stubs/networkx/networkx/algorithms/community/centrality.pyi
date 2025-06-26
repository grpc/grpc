from _typeshed import Incomplete
from collections.abc import Generator

from networkx.utils.backends import _dispatch

@_dispatch
def girvan_newman(G, most_valuable_edge: Incomplete | None = None) -> Generator[Incomplete, None, Incomplete]: ...
