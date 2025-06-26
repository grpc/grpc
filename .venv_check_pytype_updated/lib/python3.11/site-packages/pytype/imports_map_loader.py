"""Import and set up the imports_map."""

import collections
import logging
import os

from pytype import imports_map
from pytype.platform_utils import path_utils

log = logging.getLogger(__name__)


# Type aliases.
_MultimapType = dict[str, list[str]]
_ItemType = tuple[str, str]


class ImportsMapBuilder:
  """Build an imports map from (short_path, path) pairs."""

  def __init__(self, options):
    self.options = options

  def _read_from_file(self, path) -> list[_ItemType]:
    """Read the imports_map file."""
    items = []
    with self.options.open_function(path) as f:
      for line in f:
        line = line.strip()
        if line:
          short_path, path = line.split(" ", 1)
          items.append((short_path, path))
    return items

  def _build_multimap(self, items: list[_ItemType]) -> _MultimapType:
    """Build a multimap from a list of (short_path, path) pairs."""
    # TODO(mdemello): Keys should ideally be modules, not short paths.
    imports_multimap = collections.defaultdict(set)
    for short_path, path in items:
      short_path, _ = path_utils.splitext(short_path)  # drop extension
      imports_multimap[short_path].add(path)
    # Sort the multimap. Move items with '#' in the base name, generated for
    # analysis results via --api, first, so we prefer them over others.
    return {
        short_path: sorted(paths, key=path_utils.basename)
        for short_path, paths in imports_multimap.items()
    }

  def _finalize(
      self, imports_multimap: _MultimapType
  ) -> imports_map.ImportsMap:
    """Generate the final imports map."""
    # The path `%` can be used to specify unused files, so pytype can emit them
    # as part of the --unused_imports_info_files option.
    unused_files = imports_multimap.pop("%", [])

    # Output warnings for all multiple mappings and keep the lexicographically
    # first path for each.
    for short_path, paths in imports_multimap.items():
      if len(paths) > 1:
        log.warning(
            "Multiple files for %r => %r ignoring %r",
            short_path,
            paths[0],
            paths[1:],
        )
        unused_files.extend(paths[1:])
    imports = {
        short_path: path_utils.abspath(paths[0])
        for short_path, paths in imports_multimap.items()
    }

    dir_paths = {}
    intermediate_dirs = set()

    for short_path, full_path in sorted(imports.items()):
      dir_paths[short_path] = full_path
      # Collect intermediate directories.
      # For example, for foo/bar/quux.py, collect foo and foo/bar.
      # Avoid repeated work on common ancestors; it matters for huge maps.
      intermediate_dir = short_path
      while True:
        intermediate_dir = os.path.dirname(intermediate_dir)
        if not intermediate_dir or intermediate_dir in intermediate_dirs:
          break
        intermediate_dirs.add(intermediate_dir)

    # Add the potential directory nodes for adding "__init__", because some
    # build systems automatically create __init__.py in empty directories. These
    # are added with the path name appended with "/", mapping to the empty file.
    # See also load_pytd._import_file which also checks for an empty directory
    # and acts as if an empty __init__.py is there.
    for intermediate_dir in intermediate_dirs:
      intermediate_dir_init = os.path.join(intermediate_dir, "__init__")
      if intermediate_dir_init not in dir_paths:
        log.warning("Created empty __init__ %r", intermediate_dir_init)
        dir_paths[intermediate_dir_init] = os.devnull

    return imports_map.ImportsMap(items=dir_paths, unused=unused_files)

  def build_from_file(self, path: str | None) -> imports_map.ImportsMap | None:
    """Create an ImportsMap from a .imports_info file.

    Builds a dict of short_path to full name
       (e.g. "path/to/file.py" =>
             "$GENDIR/rulename~~pytype-gen/path_to_file.py~~pytype"
    Args:
      path: The file with the info (may be None, for do-nothing)

    Returns:
      Dict of .py short_path to list of .pytd path or None if no path
    """
    if not path:
      return None
    items = self._read_from_file(path)
    return self.build_from_items(items)

  def build_from_items(
      self, items: list[_ItemType] | None
  ) -> imports_map.ImportsMap | None:
    """Create a file mapping from a list of (short path, path) tuples.

    Builds a dict of short_path to full name
       (e.g. "path/to/file.py" =>
             "$GENDIR/rulename~~pytype-gen/path_to_file.py~~pytype"
    Args:
      items: A list of (short_path, full_path) tuples.

    Returns:
      Dict of .py short_path to list of .pytd path or None if no items
    """
    if not items:
      return None
    imports_multimap = self._build_multimap(items)
    assert imports_multimap is not None
    return self._finalize(imports_multimap)
