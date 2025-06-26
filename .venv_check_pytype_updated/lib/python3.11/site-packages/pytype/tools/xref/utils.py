"""Utilities for xref."""

from pytype import module_utils
from pytype.platform_utils import path_utils


def get_module_filepath(module_filename):
  """Recover the path to the py file from a module pyi path."""

  def _clean(path):
    """Change extension to .py."""
    prefix, fname = path_utils.split(path)
    fname, _ = path_utils.splitext(fname)
    path = path_utils.join(prefix, fname + ".py")
    return path

  return _clean(module_filename)


def process_imports_map(imports_map):
  """Generate a map of {module name: canonical relative path}."""
  if not imports_map:
    return {}

  # Store maps of the full path and canonical relative path.
  mod_to_fp = {}
  fp_to_cp = {}

  for path, v in imports_map.items():
    mod = module_utils.path_to_module_name(path)
    mod_to_fp[mod] = v
    if v not in fp_to_cp or len(path) > len(fp_to_cp[v]):
      fp_to_cp[v] = path

  return {mod: fp_to_cp[fp] for mod, fp in mod_to_fp.items()}
