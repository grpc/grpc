"""Argument parsing for tools that pass args on to pytype_single."""

import argparse
import dataclasses
from typing import Any

from pytype import config as pytype_config


# Type alias
_ArgDict = dict[str, Any]
Namespace = argparse.Namespace


@dataclasses.dataclass
class ParsedArgs:
  """Parsed and processed args for the tool and pytype."""

  tool_args: Namespace
  pytype_opts: pytype_config.Options
  all_args: Namespace

  def __init__(self, tool_args: Namespace, pytype_opts: pytype_config.Options):
    self.tool_args = tool_args
    self.pytype_opts = pytype_opts
    # Add a namespace with both sets of args merged.
    args = dict(vars(tool_args))
    args.update(self.pytype_opts.as_dict())
    self.all_args = argparse.Namespace(**args)


class Parser:
  """Parser that integrates tool and pytype-single args."""

  def __init__(self, parser, *, pytype_single_args=None, overrides=None):
    """Initialize a parser.

    Args:
      parser: An argparse.ArgumentParser or compatible object
      pytype_single_args: Args passed to pytype
      overrides: Pytype args that the tool overrides (will be put into the tool
        args, with the corresponding pytype opts getting their default values)
    """
    self._parser = parser
    self._overrides = overrides or []
    self.pytype_single_args = pytype_single_args or {}

  def parse_args(self, argv: list[str]) -> ParsedArgs:
    """Parses argv.

    Args:
      argv: sys.argv[1:]

    Returns:
      A ParsedArgs object
    """
    tool_args = self._parser.parse_args(argv)
    return self.process_parsed_args(tool_args)

  def get_pytype_kwargs(self, args: argparse.Namespace) -> _ArgDict:
    """Return a set of kwargs to pass to pytype.config.Options.

    Args:
      args: an argparse.Namespace.

    Returns:
      A dict of kwargs with pytype_single args as keys.
    """
    return {k: getattr(args, k) for k in self.pytype_single_args}

  def process_parsed_args(self, tool_args: Namespace) -> ParsedArgs:
    """Process args from a namespace."""
    pytype_args = pytype_config.make_parser().parse_args([])
    pytype_dict = vars(pytype_args)
    tool_dict = {}
    for k, v in vars(tool_args).items():
      if (
          k in self.pytype_single_args
          and k not in self._overrides
          and k in pytype_dict
      ):
        pytype_dict[k] = v
      else:
        tool_dict[k] = v
    tool_args = Namespace(**tool_dict)
    self.process(tool_args, pytype_args)
    self._ensure_valid_pytype_args(pytype_args)
    pytype_opts = pytype_config.Options(pytype_args)
    return ParsedArgs(tool_args, pytype_opts)

  def process(self, tool_args, pytype_args):
    """Process raw pytype args before passing to config.Options."""
    # Override in subclasses

  def error(self, msg):
    self._parser.error(msg)

  def _ensure_valid_pytype_args(self, pytype_args: argparse.Namespace):
    """Final adjustment of raw pytype args before constructing Options."""
    # If we do not have an input file add a dummy one here; tools often need to
    # construct a config.Options without having an input file.
    if not getattr(pytype_args, "input", None):
      pytype_args.input = ["<dummy_file>"]

    if isinstance(pytype_args.input, str):
      pytype_args.input = [pytype_args.input]

    # If we are passed an imports map we should look for pickled files as well.
    if getattr(pytype_args, "imports_map", None):
      pytype_args.use_pickled_files = True
