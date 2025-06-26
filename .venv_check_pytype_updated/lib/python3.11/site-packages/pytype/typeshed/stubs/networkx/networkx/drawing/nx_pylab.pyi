from _typeshed import Incomplete

def draw(G, pos: Incomplete | None = None, ax: Incomplete | None = None, **kwds) -> None: ...
def draw_networkx(
    G, pos: Incomplete | None = None, arrows: Incomplete | None = None, with_labels: bool = True, **kwds
) -> None: ...
def draw_networkx_nodes(
    G,
    pos,
    nodelist: Incomplete | None = None,
    node_size: Incomplete | int = 300,
    node_color: str = "#1f78b4",
    node_shape: str = "o",
    alpha: Incomplete | None = None,
    cmap: Incomplete | None = None,
    vmin: Incomplete | None = None,
    vmax: Incomplete | None = None,
    ax: Incomplete | None = None,
    linewidths: Incomplete | None = None,
    edgecolors: Incomplete | None = None,
    label: Incomplete | None = None,
    margins: Incomplete | None = None,
): ...
def draw_networkx_edges(
    G,
    pos,
    edgelist: Incomplete | None = None,
    width: float = 1.0,
    edge_color: str = "k",
    style: str = "solid",
    alpha: Incomplete | None = None,
    arrowstyle: Incomplete | None = None,
    arrowsize: int = 10,
    edge_cmap: Incomplete | None = None,
    edge_vmin: Incomplete | None = None,
    edge_vmax: Incomplete | None = None,
    ax: Incomplete | None = None,
    arrows: Incomplete | None = None,
    label: Incomplete | None = None,
    node_size: Incomplete | int = 300,
    nodelist: Incomplete | None = None,
    node_shape: str = "o",
    connectionstyle: str = "arc3",
    min_source_margin: int = 0,
    min_target_margin: int = 0,
): ...
def draw_networkx_labels(
    G,
    pos,
    labels: Incomplete | None = None,
    font_size: int = 12,
    font_color: str = "k",
    font_family: str = "sans-serif",
    font_weight: str = "normal",
    alpha: Incomplete | None = None,
    bbox: Incomplete | None = None,
    horizontalalignment: str = "center",
    verticalalignment: str = "center",
    ax: Incomplete | None = None,
    clip_on: bool = True,
): ...
def draw_networkx_edge_labels(
    G,
    pos,
    edge_labels: Incomplete | None = None,
    label_pos: float = 0.5,
    font_size: int = 10,
    font_color: str = "k",
    font_family: str = "sans-serif",
    font_weight: str = "normal",
    alpha: Incomplete | None = None,
    bbox: Incomplete | None = None,
    horizontalalignment: str = "center",
    verticalalignment: str = "center",
    ax: Incomplete | None = None,
    rotate: bool = True,
    clip_on: bool = True,
): ...
def draw_circular(G, **kwargs) -> None: ...
def draw_kamada_kawai(G, **kwargs) -> None: ...
def draw_random(G, **kwargs) -> None: ...
def draw_spectral(G, **kwargs) -> None: ...
def draw_spring(G, **kwargs) -> None: ...
def draw_shell(G, nlist: Incomplete | None = None, **kwargs) -> None: ...
def draw_planar(G, **kwargs) -> None: ...
