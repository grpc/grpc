import tkinter
from tkinter import ttk

from ._widget import ThemedWidget

class ThemedStyle(ttk.Style, ThemedWidget):
    def __init__(
        self, master: tkinter.Misc | None = ..., *, theme: str | None = ..., gif_override: bool | None = ..., **kwargs
    ) -> None: ...
    # theme_use() can't return None (differs from ttk.Style)
    def theme_use(self, theme_name: str | None = None) -> str: ...  # type: ignore[override]
    def theme_names(self) -> list[str]: ...  # type: ignore[override]
