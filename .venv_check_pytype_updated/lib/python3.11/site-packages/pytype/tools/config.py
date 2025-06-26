"""Utilities for dealing with project configuration."""

import abc
from collections.abc import Iterable
import configparser
from typing import TypeVar

from pytype.platform_utils import path_utils
import toml

_CONFIG_FILENAMES = ('pyproject.toml', 'setup.cfg')
_ConfigSectionT = TypeVar('_ConfigSectionT', bound='ConfigSection')


def find_config_file(path):
  """Finds the first instance of a config file in a prefix of path."""

  # Make sure path is a directory
  if not path_utils.isdir(path):
    path = path_utils.dirname(path)

  # Guard against symlink loops and /
  seen = set()
  while path and path not in seen:
    seen.add(path)
    for filename in _CONFIG_FILENAMES:
      f = path_utils.join(path, filename)
      if path_utils.exists(f) and path_utils.isfile(f):
        return f
    path = path_utils.dirname(path)

  return None


class ConfigSection(abc.ABC):
  """A section of a config file."""

  @classmethod
  @abc.abstractmethod
  def create_from_file(
      cls: type[_ConfigSectionT], filepath: str, section: str
  ) -> _ConfigSectionT:
    """Create a ConfigSection if the file at filepath has section."""

  @abc.abstractmethod
  def items(self) -> Iterable[tuple[str, str]]:
    ...


class TomlConfigSection(ConfigSection):
  """A section of a TOML config file."""

  def __init__(self, content):
    self._content = content

  @classmethod
  def create_from_file(cls, filepath, section):
    try:
      content = toml.load(filepath)
    except toml.TomlDecodeError:
      return None
    if 'tool' in content and section in content['tool']:
      return cls(content['tool'][section])
    return None

  def items(self):
    for k, v in self._content.items():
      yield (k, ' '.join(str(e) for e in v) if isinstance(v, list) else str(v))


class IniConfigSection(ConfigSection):
  """A section of an INI config file."""

  def __init__(self, parser, section):
    self._parser = parser
    self._section = section

  @classmethod
  def create_from_file(cls, filepath, section):
    parser = configparser.ConfigParser()
    try:
      parser.read(filepath)
    except configparser.MissingSectionHeaderError:
      # We've read an improperly formatted config file.
      return None
    if parser.has_section(section):
      return cls(parser, section)
    return None

  def items(self):
    return self._parser.items(self._section)
