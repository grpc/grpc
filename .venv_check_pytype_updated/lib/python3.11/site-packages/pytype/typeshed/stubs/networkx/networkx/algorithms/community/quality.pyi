from networkx.exception import NetworkXError
from networkx.utils.backends import _dispatch

__all__ = ["modularity", "partition_quality"]

class NotAPartition(NetworkXError):
    def __init__(self, G, collection) -> None: ...

@_dispatch
def modularity(G, communities, weight: str = "weight", resolution: float = 1): ...
@_dispatch
def partition_quality(G, partition): ...
