from _typeshed import Incomplete
from collections.abc import Hashable, Iterable, Mapping, Sequence
from typing import Literal
from typing_extensions import Self, TypeAlias

import numpy as np
from matplotlib.axes import Axes
from matplotlib.colors import Colormap, ListedColormap
from matplotlib.gridspec import GridSpec
from matplotlib.typing import ColorType
from numpy._typing import _ArrayLike, _ArrayLikeInt_co
from numpy.typing import ArrayLike, NDArray
from pandas import DataFrame, Index, Series

from .axisgrid import Grid

# pandas._typing.ListLikeU is partially Unknown
_ListLikeU: TypeAlias = Sequence[Incomplete] | np.ndarray[Incomplete, Incomplete] | Series[Incomplete] | Index[Incomplete]

__all__ = ["heatmap", "clustermap"]

def heatmap(
    data: DataFrame | _ArrayLike[Incomplete],
    *,
    vmin: float | None = None,
    vmax: float | None = None,
    cmap: str | list[ColorType] | Colormap | None = None,
    center: float | None = None,
    robust: bool = False,
    annot: bool | ArrayLike | None = None,
    fmt: str = ".2g",
    annot_kws: dict[str, Incomplete] | None = None,
    linewidths: float = 0,
    linecolor: ColorType = "white",
    cbar: bool = True,
    cbar_kws: dict[str, Incomplete] | None = None,
    cbar_ax: Axes | None = None,
    square: bool = False,
    xticklabels: Literal["auto"] | bool | int | Sequence[str] = "auto",
    yticklabels: Literal["auto"] | bool | int | Sequence[str] = "auto",
    mask: NDArray[np.bool_] | DataFrame | None = None,
    ax: Axes | None = None,
    **kwargs: Incomplete,
) -> Axes: ...

class _DendrogramPlotter:
    axis: int
    array: NDArray[Incomplete]
    data: DataFrame
    shape: tuple[int, int]
    metric: str
    method: str
    label: bool
    rotate: bool
    linkage: NDArray[Incomplete]
    dendrogram: dict[str, list[Incomplete]]
    xticks: list[float] | NDArray[Incomplete]
    yticks: list[float] | NDArray[Incomplete]
    xticklabels: list[str]
    yticklabels: list[str]
    ylabel: str
    xlabel: str
    dependent_coord: list[list[float]]
    independent_coord: list[list[float]]
    def __init__(
        self, data: DataFrame, linkage: NDArray[Incomplete] | None, metric: str, method: str, axis: int, label: bool, rotate: bool
    ) -> None: ...
    @property
    def calculated_linkage(self) -> NDArray[Incomplete]: ...
    def calculate_dendrogram(self) -> dict[str, list[Incomplete]]: ...
    @property
    def reordered_ind(self) -> list[int]: ...
    def plot(self, ax: Axes, tree_kws: dict[str, Incomplete]) -> Self: ...

def dendrogram(
    data: DataFrame,
    *,
    linkage: NDArray[Incomplete] | None = None,
    axis: int = 1,
    label: bool = True,
    metric: str = "euclidean",
    method: str = "average",
    rotate: bool = False,
    tree_kws: dict[str, Incomplete] | None = None,
    ax: Axes | None = None,
) -> _DendrogramPlotter: ...

class ClusterGrid(Grid):
    data: DataFrame
    data2d: DataFrame
    mask: DataFrame
    row_colors: Incomplete
    row_color_labels: Incomplete
    col_colors: Incomplete
    col_color_labels: Incomplete
    gs: GridSpec
    ax_row_dendrogram: Axes
    ax_col_dendrogram: Axes
    ax_row_colors: Axes | None
    ax_col_colors: Axes | None
    ax_heatmap: Axes
    ax_cbar: Axes | None
    cax: Axes | None
    cbar_pos: Incomplete
    dendrogram_row: _DendrogramPlotter | None
    dendrogram_col: _DendrogramPlotter | None
    def __init__(
        self,
        data: (
            _ListLikeU
            | DataFrame
            | dict[Incomplete, Incomplete]
            | Iterable[_ListLikeU | tuple[Hashable, _ListLikeU] | dict[Incomplete, Incomplete]]
            | None
        ),
        pivot_kws: Mapping[str, Incomplete] | None = None,
        z_score: int | None = None,
        standard_scale: int | None = None,
        figsize: tuple[float, float] | None = None,
        row_colors: Incomplete | None = None,
        col_colors: Incomplete | None = None,
        mask: NDArray[np.bool_] | DataFrame | None = None,
        dendrogram_ratio: float | tuple[float, float] | None = None,
        colors_ratio: float | tuple[float, float] | None = None,
        cbar_pos: tuple[float, float, float, float] | None = None,
    ) -> None: ...
    def format_data(
        self,
        data: DataFrame,
        pivot_kws: Mapping[str, Incomplete] | None,
        z_score: int | None = None,
        standard_scale: int | None = None,
    ) -> DataFrame: ...
    @staticmethod
    def z_score(data2d: DataFrame, axis: int = 1) -> DataFrame: ...
    @staticmethod
    def standard_scale(data2d: DataFrame, axis: int = 1) -> DataFrame: ...
    def dim_ratios(self, colors: Incomplete | None, dendrogram_ratio: float, colors_ratio: float) -> list[float]: ...
    @staticmethod
    def color_list_to_matrix_and_cmap(
        colors: Sequence[ColorType], ind: _ArrayLikeInt_co, axis: int = 0
    ) -> tuple[NDArray[np.int_], ListedColormap]: ...
    def plot_dendrograms(
        self,
        row_cluster: bool,
        col_cluster: bool,
        metric: str,
        method: str,
        row_linkage: NDArray[Incomplete] | None,
        col_linkage: NDArray[Incomplete] | None,
        tree_kws: dict[str, Incomplete] | None,
    ) -> None: ...
    def plot_colors(self, xind: _ArrayLikeInt_co, yind: _ArrayLikeInt_co, **kws: Incomplete) -> None: ...
    def plot_matrix(
        self, colorbar_kws: dict[str, Incomplete], xind: _ArrayLikeInt_co, yind: _ArrayLikeInt_co, **kws: Incomplete
    ) -> None: ...
    def plot(
        self,
        metric: str,
        method: str,
        colorbar_kws: dict[str, Incomplete] | None,
        row_cluster: bool,
        col_cluster: bool,
        row_linkage: NDArray[Incomplete] | None,
        col_linkage: NDArray[Incomplete] | None,
        tree_kws: dict[str, Incomplete] | None,
        **kws: Incomplete,
    ) -> Self: ...

def clustermap(
    data: (
        _ListLikeU
        | DataFrame
        | dict[Incomplete, Incomplete]
        | Iterable[_ListLikeU | tuple[Hashable, _ListLikeU] | dict[Incomplete, Incomplete]]
        | None
    ),
    *,
    pivot_kws: dict[str, Incomplete] | None = None,
    method: str = "average",
    metric: str = "euclidean",
    z_score: int | None = None,
    standard_scale: int | None = None,
    figsize: tuple[float, float] | None = (10, 10),
    cbar_kws: dict[str, Incomplete] | None = None,
    row_cluster: bool = True,
    col_cluster: bool = True,
    row_linkage: NDArray[Incomplete] | None = None,
    col_linkage: NDArray[Incomplete] | None = None,
    row_colors: Incomplete | None = None,
    col_colors: Incomplete | None = None,
    mask: NDArray[np.bool_] | DataFrame | None = None,
    dendrogram_ratio: float | tuple[float, float] = 0.2,
    colors_ratio: float | tuple[float, float] = 0.03,
    cbar_pos: tuple[float, float, float, float] | None = (0.02, 0.8, 0.05, 0.18),
    tree_kws: dict[str, Incomplete] | None = None,
    **kwargs: Incomplete,
) -> ClusterGrid: ...
