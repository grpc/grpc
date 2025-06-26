from _typeshed import Incomplete

from networkx.utils.backends import _dispatch

from .edmondskarp import edmonds_karp

__all__ = ["gomory_hu_tree"]

default_flow_func = edmonds_karp

@_dispatch
def gomory_hu_tree(G, capacity: str = "capacity", flow_func: Incomplete | None = None): ...
