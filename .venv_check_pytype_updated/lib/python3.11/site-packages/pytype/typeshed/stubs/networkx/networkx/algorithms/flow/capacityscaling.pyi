from networkx.utils.backends import _dispatch

@_dispatch
def capacity_scaling(G, demand: str = "demand", capacity: str = "capacity", weight: str = "weight", heap=...): ...
