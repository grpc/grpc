from networkx.utils.backends import _dispatch

@_dispatch
def sudoku_graph(n: int = 3): ...
