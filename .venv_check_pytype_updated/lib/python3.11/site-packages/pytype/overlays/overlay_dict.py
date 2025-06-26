"""A dictionary of module names to pytype overlays.

Some libraries need custom overlays to provide useful type information. Pytype
has some built-in overlays, and additional overlays may be added to the overlays
dictionary. See overlay.py for the overlay interface and the files under
overlays/ for examples.

Each entry in overlays maps the module name to the overlay object
"""

from pytype.overlays import abc_overlay
from pytype.overlays import asyncio_types_overlay
from pytype.overlays import attr_overlay
from pytype.overlays import chex_overlay
from pytype.overlays import collections_overlay
from pytype.overlays import dataclass_overlay
from pytype.overlays import enum_overlay
from pytype.overlays import fiddle_overlay
from pytype.overlays import flax_overlay
from pytype.overlays import functools_overlay
from pytype.overlays import future_overlay
from pytype.overlays import pytype_extensions_overlay
from pytype.overlays import six_overlay
from pytype.overlays import subprocess_overlay
from pytype.overlays import sys_overlay
from pytype.overlays import typing_extensions_overlay
from pytype.overlays import typing_overlay

# Collection of module overlays, used by the vm to fetch an overlay
# instead of the module itself. Memoized in the vm itself.
overlays = {
    "abc": abc_overlay.ABCOverlay,
    "asyncio": asyncio_types_overlay.AsyncioOverlay,
    "attr": attr_overlay.AttrOverlay,
    "attrs": attr_overlay.AttrsOverlay,
    "chex": chex_overlay.ChexOverlay,
    "collections": collections_overlay.CollectionsOverlay,
    "collections.abc": collections_overlay.ABCOverlay,
    "dataclasses": dataclass_overlay.DataclassOverlay,
    "enum": enum_overlay.EnumOverlay,
    "fiddle": fiddle_overlay.FiddleOverlay,
    "flax.struct": flax_overlay.DataclassOverlay,
    "flax.linen": flax_overlay.LinenOverlay,
    "flax.linen.module": flax_overlay.LinenModuleOverlay,
    "functools": functools_overlay.FunctoolsOverlay,
    "future.utils": future_overlay.FutureUtilsOverlay,
    "pytype_extensions": pytype_extensions_overlay.PytypeExtensionsOverlay,
    "six": six_overlay.SixOverlay,
    "subprocess": subprocess_overlay.SubprocessOverlay,
    "sys": sys_overlay.SysOverlay,
    "types": asyncio_types_overlay.TypesOverlay,
    "typing": typing_overlay.TypingOverlay,
    "typing_extensions": typing_extensions_overlay.TypingExtensionsOverlay,
}
