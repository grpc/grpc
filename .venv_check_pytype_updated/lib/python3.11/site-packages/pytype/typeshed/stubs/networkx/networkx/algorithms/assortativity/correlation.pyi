from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

@_dispatch
def degree_assortativity_coefficient(
    G, x: str = "out", y: str = "in", weight: Incomplete | None = None, nodes: Incomplete | None = None
): ...
@_dispatch
def degree_pearson_correlation_coefficient(
    G, x: str = "out", y: str = "in", weight: Incomplete | None = None, nodes: Incomplete | None = None
): ...
@_dispatch
def attribute_assortativity_coefficient(G, attribute, nodes: Incomplete | None = None): ...
@_dispatch
def numeric_assortativity_coefficient(G, attribute, nodes: Incomplete | None = None): ...
