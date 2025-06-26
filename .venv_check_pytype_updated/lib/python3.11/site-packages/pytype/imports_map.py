"""Imports map data structure."""

from collections.abc import Mapping, Sequence
import dataclasses


@dataclasses.dataclass(frozen=True)
class ImportsMap:
  """Parsed --imports_info.

  Attributes:
    items: Map from module path to full file path on disk.
    unused: Unused files. Stored here, so they can be emitted as unused inputs.
      See --unused_imports_info_files option.
  """

  items: Mapping[str, str] = dataclasses.field(default_factory=dict)
  unused: Sequence[str] = dataclasses.field(default_factory=list)

  def __getitem__(self, key: str):
    return self.items[key]

  def __contains__(self, key: str):
    return key in self.items

  def __len__(self):
    return len(self.items)
