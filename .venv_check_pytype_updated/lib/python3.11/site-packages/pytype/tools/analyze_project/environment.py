"""Utils for creating importlab environment."""

from importlab import environment
from importlab import fs

from pytype import utils


class PytdFileSystem(fs.ExtensionRemappingFileSystem):
  """File system that remaps .py file extensions to pytd."""

  def __init__(self, underlying):
    super().__init__(underlying, 'pytd')


def create_importlab_environment(conf, typeshed):
  """Create an importlab environment from the python version and path."""
  python_version = utils.version_from_string(conf.python_version)
  path = fs.Path()
  for p in conf.pythonpath:
    path.add_path(p, 'os')
  for p in typeshed.get_pytd_paths():
    path.add_fs(PytdFileSystem(fs.OSFileSystem(p)))
  for p in typeshed.get_typeshed_paths():
    path.add_path(p, 'pyi')
  return environment.Environment(path, python_version)
