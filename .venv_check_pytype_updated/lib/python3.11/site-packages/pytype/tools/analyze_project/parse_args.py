"""Argument parsing for analyze_project."""

import argparse

from pytype import config as pytype_config
from pytype import datatypes
from pytype.tools.analyze_project import config


_ARG_PREFIX = '--'


def string_to_bool(s):
  return s == 'True' if s in ('True', 'False') else s


def convert_string(s):
  s = s.replace('\n', '')
  try:
    return int(s)
  except ValueError:
    return string_to_bool(s)


class Parser:
  """Parser with additional functions for config file processing."""

  def __init__(self, parser, pytype_single_args):
    """Initialize a parser.

    Args:
      parser: An argparse.ArgumentParser or compatible object
      pytype_single_args: Iterable of args that will be passed to pytype_single
    """
    self._parser = parser
    self.pytype_single_args = pytype_single_args
    self._pytype_arg_map = pytype_config.args_map()

  def create_initial_args(self, keys):
    """Creates the initial set of args."""
    return argparse.Namespace(**{k: None for k in keys})

  def config_from_defaults(self):
    defaults = self._parser.parse_args([])
    self.postprocess(defaults)
    conf = config.Config(*self.pytype_single_args)
    conf.populate_from(defaults)
    return conf

  def clean_args(self, args, keys):
    """Clean None values out of the arg namespace.

    This lets us check for a config file arg based on whether the None default
    was overwritten.

    Args:
      args: an argparse.Namespace.
      keys: Keys to clean if None
    """
    for k in keys:
      if getattr(args, k) is None:
        delattr(args, k)

  def parse_args(self, argv):
    """Parses argv.

    Commandline-only args are parsed normally. File-configurable args appear in
    the parsed args only if explicitly present in argv.

    Args:
      argv: sys.argv[1:]

    Returns:
      An argparse.Namespace.
    """
    file_config_names = set(config.ITEMS) | set(self.pytype_single_args)
    args = self.create_initial_args(file_config_names)
    self._parser.parse_args(argv, args)
    self.clean_args(args, file_config_names)
    self.postprocess(args)
    return args

  def convert_strings(self, args: argparse.Namespace):
    """Converts strings in an args namespace to values."""
    for k in self.pytype_single_args:
      if hasattr(args, k):
        v = getattr(args, k)
        assert isinstance(v, str)
        setattr(args, k, convert_string(v))

  def postprocess(self, args: argparse.Namespace):
    """Postprocesses the subset of pytype_single_args that appear in args.

    Args:
      args: an argparse.Namespace.
    """
    names = {k for k in self.pytype_single_args if hasattr(args, k)}
    opt_map = {k: self._pytype_arg_map[k].long_opt for k in names}
    pytype_config.Postprocessor(names, opt_map, args).process()

  def error(self, message):
    self._parser.error(message)


def make_parser():
  """Make parser for command line args.

  Returns:
    A Parser object.
  """

  parser = argparse.ArgumentParser(usage='%(prog)s [options] input [input ...]')
  parser.register('action', 'flatten', _FlattenAction)
  modes = parser.add_mutually_exclusive_group()
  modes.add_argument(
      '--tree', dest='tree', action='store_true', default=False,
      help='Display import tree.')
  modes.add_argument(
      '--unresolved', dest='unresolved', action='store_true', default=False,
      help='Display unresolved dependencies.')
  modes.add_argument(
      '--generate-config', dest='generate_config', type=str, action='store',
      default='',
      help='Write out a dummy configuration file.')
  parser.add_argument(
      '-v', '--verbosity', dest='verbosity', type=int, action='store',
      default=1,
      help='Set logging level: 0=ERROR, 1=WARNING (default), 2=INFO.')
  parser.add_argument(
      '--config', dest='config', type=str, action='store', default='',
      help='Configuration file.')
  parser.add_argument(
      '--version', action='store_true', dest='version', default=None,
      help=('Display pytype version and exit.'))

  # Adds options from the config file.
  types = config.make_converters()
  # For nargs=*, argparse calls type() on each arg individually, so
  # _FlattenAction flattens the list of sets of paths as we go along.
  for option in [
      (('-x', '--exclude'), {'nargs': '*', 'action': 'flatten'}),
      (('inputs',), {'metavar': 'input', 'nargs': '*', 'action': 'flatten'}),
      (('-k', '--keep-going'), {'action': 'store_true', 'type': None}),
      (('-j', '--jobs'), {'action': 'store', 'metavar': 'N'}),
      (('--platform',),),
      (('-P', '--pythonpath'),),
      (('-V', '--python-version'),)
  ]:
    _add_file_argument(parser, types, *option)
  output = parser.add_mutually_exclusive_group()
  _add_file_argument(output, types, ('-o', '--output'))
  output.add_argument(
      '-n', '--no-cache', dest='no_cache', action='store_true', default=False,
      help='Send pytype output to a temporary directory.')

  # Adds options from pytype-single.
  wrapper = datatypes.ParserWrapper(parser)
  pytype_config.add_basic_options(wrapper)
  pytype_config.add_feature_flags(wrapper)
  return Parser(parser, pytype_single_args=wrapper.actions)


class _FlattenAction(argparse.Action):
  """Flattens a list of sets. Used by --exclude and inputs."""

  def __call__(self, parser, namespace, values, option_string=None):
    items = getattr(namespace, self.dest, None) or set()
    # We want to keep items as None if values is empty, since that means the
    # argument was not passed on the command line. Note that an empty values
    # can occur for inputs but not --exclude because a positional argument
    # tries to overwrite any existing default with its own.
    if values:
      setattr(namespace, self.dest, items)
      for v in values:
        items.update(v)


def _add_file_argument(parser, types, args, custom_kwargs=None):
  """Add a file-configurable option to the parser.

  Args:
    parser: The parser.
    types: A map from option destination to type.
    args: The option's name(s). Either a 2-tuple of (short_arg, arg) or a
      1-tuple of (arg,).
    custom_kwargs: The option's custom kwargs.
  """
  custom_kwargs = custom_kwargs or {}
  arg = args[-1]
  dest = custom_kwargs.get('dest', arg.lstrip(_ARG_PREFIX).replace('-', '_'))
  kwargs = {'type': types.get(dest),
            'action': 'store',
            'default': config.ITEMS[dest].default,
            'help': config.ITEMS[dest].comment}
  kwargs.update(custom_kwargs)  # custom_kwargs takes precedence
  if kwargs['type'] is None:
    # None is the default anyway, and for some action types, supplying `type` is
    # a type error.
    del kwargs['type']
  if arg.startswith(_ARG_PREFIX):
    # For an optional argument, `dest` should be explicitly given. (For a
    # positional one, it's inferred from `arg`.)
    kwargs['dest'] = dest
  elif 'type' in kwargs:
    # For a positional argument, the type function isn't applied to the default,
    # so we do the transformation manually.
    kwargs['default'] = kwargs['type'](kwargs['default'])
  parser.add_argument(*args, **kwargs)
