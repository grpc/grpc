from _typeshed import Incomplete

def random_layout(G, center: Incomplete | None = None, dim: int = 2, seed: Incomplete | None = None): ...
def circular_layout(G, scale: float = 1, center: Incomplete | None = None, dim: int = 2): ...
def shell_layout(
    G,
    nlist: Incomplete | None = None,
    rotate: Incomplete | None = None,
    scale: float = 1,
    center: Incomplete | None = None,
    dim: int = 2,
): ...
def bipartite_layout(
    G, nodes, align: str = "vertical", scale: float = 1, center: Incomplete | None = None, aspect_ratio: float = ...
): ...
def spring_layout(
    G,
    k: Incomplete | None = None,
    pos: Incomplete | None = None,
    fixed: Incomplete | None = None,
    iterations: int = 50,
    threshold: float = 0.0001,
    weight: str = "weight",
    scale: float = 1,
    center: Incomplete | None = None,
    dim: int = 2,
    seed: Incomplete | None = None,
): ...

fruchterman_reingold_layout = spring_layout

def kamada_kawai_layout(
    G,
    dist: Incomplete | None = None,
    pos: Incomplete | None = None,
    weight: str = "weight",
    scale: float = 1,
    center: Incomplete | None = None,
    dim: int = 2,
): ...
def spectral_layout(G, weight: str = "weight", scale: float = 1, center: Incomplete | None = None, dim: int = 2): ...
def planar_layout(G, scale: float = 1, center: Incomplete | None = None, dim: int = 2): ...
def spiral_layout(
    G, scale: float = 1, center: Incomplete | None = None, dim: int = 2, resolution: float = 0.35, equidistant: bool = False
): ...
def multipartite_layout(
    G, subset_key: str = "subset", align: str = "vertical", scale: float = 1, center: Incomplete | None = None
): ...
def arf_layout(
    G,
    pos: Incomplete | None = None,
    scaling: float = 1,
    a: float = 1.1,
    etol: float = 1e-06,
    dt: float = 0.001,
    max_iter: int = 1000,
): ...
def rescale_layout(pos, scale: float = 1): ...
def rescale_layout_dict(pos, scale: float = 1): ...
