from networkx.utils.backends import _dispatch

@_dispatch
def s_metric(G, normalized: bool = True): ...
