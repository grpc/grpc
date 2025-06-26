from networkx.utils.backends import _dispatch

@_dispatch
def is_partition(G, communities): ...
