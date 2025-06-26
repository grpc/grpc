"""Representation of modules."""

from collections.abc import Sequence
import dataclasses
import os

from pytype.platform_utils import path_utils


@dataclasses.dataclass(eq=True, frozen=True)
class Module:
  """Inferred information about a module.

  Attributes:
    path: The path to the module, e.g., foo/.
    target: The filename relative to the path, e.g., bar/baz.py.
    name: The module name, e.g., bar.baz.
    kind: The module kind: Builtin, Direct, Local, or System. See
      https://github.com/google/importlab/blob/main/importlab/resolve.py.
    full_path: The full path to the module (path + target).
  """

  path: str
  target: str
  name: str
  kind: str = "Local"

  @property
  def full_path(self):
    return path_utils.join(self.path, self.target)


def infer_module(filename, pythonpath):
  """Convert a filename to a module relative to pythonpath.

  This method tries to deduce the module name from the pythonpath and the
  filename. This will not always be possible. (It depends on the filename
  starting with an entry in the pythonpath.)

  Args:
    filename: The filename of a Python file. E.g. "foo/bar/baz.py".
    pythonpath: The path Python uses to search for modules.

  Returns:
    A Module object.
  """
  # We want '' in our lookup path, but we don't want it for prefix tests.
  for path in filter(bool, pythonpath):
    if not path.endswith(path_utils.sep):
      path += path_utils.sep
    if filename.startswith(path):
      filename = filename[len(path) :]
      break
  else:
    # We have not found filename relative to anywhere in pythonpath.
    path = ""
  return Module(path, filename, path_to_module_name(filename))


def get_module_name(filename, pythonpath):
  """Get the module name, or None if we can't determine it."""
  if filename:
    filename = path_utils.normpath(filename)
    # Keep path '' as is; infer_module will handle it.
    pythonpath = [path and path_utils.normpath(path) for path in pythonpath]
    return infer_module(filename, pythonpath).name


def path_to_module_name(filename):
  """Converts a filename into a dotted module name."""
  if path_utils.dirname(filename).startswith(os.pardir):
    # Don't try to infer a module name for filenames starting with ../
    return None
  filename, ext = path_utils.splitext(filename)
  if ext and not ext.startswith(".py"):
    # If there is no extension, convert "foo/bar" to "foo.bar", since we use
    # that in our imports_info map. If there is an extension, it needs to be
    # a python source or stub file, so ".py*" should cover all the cases.
    return None
  module_name = filename.replace(path_utils.sep, ".").replace("/", ".")
  # strip __init__ suffix
  module_name, _, _ = module_name.partition(".__init__")
  return module_name


def strip_init_suffix(parts: Sequence[str]):
  return parts[:-1] if parts and parts[-1] == "__init__" else parts


def get_absolute_name(prefix, relative_name):
  """Joins a dotted-name prefix and a relative name.

  Args:
    prefix: A dotted name, e.g. foo.bar.baz
    relative_name: A dotted name with possibly some leading dots, e.g. ..x.y

  Returns:
    The relative name appended to the prefix, after going up one level for each
      leading dot.
      e.g. foo.bar.baz + ..hello.world -> foo.bar.hello.world
    None if the relative name has too many leading dots.
  """
  path = prefix.split(".") if prefix else []
  name = relative_name.lstrip(".")
  ndots = len(relative_name) - len(name)
  if ndots > len(path):
    return None
  prefix = "".join([p + "." for p in path[: len(path) + 1 - ndots]])
  return prefix + name


def get_relative_name(prefix: str, absolute_name: str) -> str:
  """Transfoms an absolute name to a relative one based on the given prefix.

  Args:
    prefix: A dotted name, e.g. foo.bar.baz
    absolute_name: A fully-qualified name, e.g. foo.bar.baz.x

  Returns:
    The absolute name with the prefix removed, with a leading dot added
      for each segment of the prefix not present in the absolute name.
      e.g. foo.bar.baz + foo.bar.hello.world -> ..hello.world
    If the prefix is disjoint from the absolute name, the absolute name is
      returned verbatim.
      e.g. foo.bar.baz + hello.world -> hello.world
    If the given absolute name has one or more leading dots, it is returned
      verbatim.
      e.g. foo.bar + ..hello.world -> ..hello.world
  """
  if absolute_name.startswith("."):
    return absolute_name
  prefix_path: list[str] = prefix.split(".") if prefix else []
  name_path: list[str] = absolute_name.split(".") if absolute_name else []

  num_match = 0
  for prefix_seg, name_seg in zip(prefix_path, name_path):
    if prefix_seg != name_seg:
      break
    num_match += 1
  # No prefix segments matched - the name is disjoint with the prefix.
  if not num_match:
    return absolute_name

  name = ".".join(name_path[num_match:])
  ndots = len(prefix_path) - num_match
  if ndots > 0:
    name = "." * (ndots + 1) + name
  return name


def get_package_name(module_name, is_package=False):
  """Figure out a package name for a module."""
  if module_name is None:
    return ""
  parts = module_name.split(".")
  if not is_package:
    parts = parts[:-1]
  return ".".join(parts)


def get_all_prefixes(module_name):
  """Return all the prefixes of a module name.

  e.g. x.y.z => x, x.y, x.y.z

  Args:
    module_name: module name

  Returns:
    List of prefixes
  """
  parts = module_name.split(".")
  name = parts[0]
  out = [name]
  for part in parts[1:]:
    name = ".".join([name, part])
    out.append(name)
  return out
