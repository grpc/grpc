from networkx.utils.backends import _dispatch

@_dispatch
def combinatorial_embedding_to_pos(embedding, fully_triangulate: bool = False): ...
