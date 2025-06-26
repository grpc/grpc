from _typeshed import Incomplete
from collections.abc import Generator

from networkx.utils.backends import _dispatch

@_dispatch
def attracting_components(G) -> Generator[Incomplete, None, None]: ...
@_dispatch
def number_attracting_components(G): ...
@_dispatch
def is_attracting_component(G): ...
