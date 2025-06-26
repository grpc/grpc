# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from dataclasses import dataclass, replace
from typing import Optional

from libcst.helpers import get_absolute_module_from_package


@dataclass(frozen=True)
class ImportItem:
    """Representation of individual import items for codemods."""

    module_name: str
    obj_name: Optional[str] = None
    alias: Optional[str] = None
    relative: int = 0

    def __post_init__(self) -> None:
        if self.module_name is None:
            object.__setattr__(self, "module_name", "")
        elif self.module_name.startswith("."):
            mod = self.module_name.lstrip(".")
            rel = self.relative + len(self.module_name) - len(mod)
            object.__setattr__(self, "module_name", mod)
            object.__setattr__(self, "relative", rel)

    @property
    def module(self) -> str:
        return "." * self.relative + self.module_name

    def resolve_relative(self, package_name: Optional[str]) -> "ImportItem":
        """Return an ImportItem with an absolute module name if possible."""
        mod = self
        # `import ..a` -> `from .. import a`
        if mod.relative and mod.obj_name is None:
            mod = replace(mod, module_name="", obj_name=mod.module_name)
        if package_name is None:
            return mod
        m = get_absolute_module_from_package(
            package_name, mod.module_name or None, self.relative
        )
        return mod if m is None else replace(mod, module_name=m, relative=0)
