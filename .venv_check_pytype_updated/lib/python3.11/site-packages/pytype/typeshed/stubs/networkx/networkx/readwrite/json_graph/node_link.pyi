from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

def node_link_data(
    G, *, source: str = "source", target: str = "target", name: str = "id", key: str = "key", link: str = "links"
): ...
@_dispatch
def node_link_graph(
    data,
    directed: bool = False,
    multigraph: bool = True,
    attrs: Incomplete | None = None,
    *,
    source: str = "source",
    target: str = "target",
    name: str = "id",
    key: str = "key",
    link: str = "links",
): ...
