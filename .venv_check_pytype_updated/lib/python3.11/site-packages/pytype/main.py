"""Tool for inferring types from Python programs.

'pytype' is a tool for generating pyi from Python programs.

Usage:
  pytype [flags] file.py
"""

import cProfile
import logging
import signal
import sys

from pytype import config
from pytype import io
from pytype import load_pytd
from pytype import metrics
from pytype import utils
from pytype.imports import typeshed


log = logging.getLogger(__name__)


class _ProfileContext:
  """A context manager for optionally profiling code."""

  def __init__(self, output_path):
    """Initialize.

    Args:
      output_path: A pathname for the profiler output.  An empty string
        indicates that no profiling should be done.
    """
    self._output_path = output_path
    self._profile = cProfile.Profile() if self._output_path else None

  def __enter__(self):
    if self._profile:
      self._profile.enable()

  def __exit__(self, exc_type, exc_value, traceback):  # pylint: disable=redefined-outer-name
    if self._profile:
      self._profile.disable()
      self._profile.dump_stats(self._output_path)


def _generate_builtins_pickle(options):
  """Create a pickled file with the standard library (typeshed + builtins)."""
  loader = load_pytd.create_loader(options)
  t = typeshed.Typeshed()
  module_names = t.get_all_module_names(options.python_version)
  blacklist = set(t.blacklisted_modules())
  for m in sorted(module_names):
    if m not in blacklist:
      loader.import_name(m)
  loader.save_to_pickle(options.generate_builtins)


def _expand_args(argv):
  """Returns argv with flagfiles expanded.

  A flagfile is an argument starting with "@". The remainder of the argument is
  interpreted as the path to a file containing a list of arguments, one per
  line. Flagfiles may contain references to other flagfiles.

  Args:
    argv: Command line arguments.
  """

  def _expand_single_arg(arg, result):
    if arg.startswith("@"):
      with open(arg[1:]) as f:
        for earg in f.read().splitlines():
          _expand_single_arg(earg, result)
    else:
      result.append(arg)

  expanded_args = []
  for arg in argv:
    _expand_single_arg(arg, expanded_args)
  return expanded_args


def _fix_spaces(argv):
  """Returns argv with unescaped spaces in paths fixed.

  This is needed for the analyze_project tool, which uses ninja to run pytype
  over single files. ninja does not escape spaces, so a path with a space is
  interpreted as multiple paths. Detect this case and merge the paths.

  Args:
    argv: Command line arguments.
  """
  escaped_argv = []
  prev_arg = None
  for arg in argv:
    if prev_arg and not prev_arg.startswith("-") and not arg.startswith("-"):
      escaped_argv[-1] += " " + arg
    else:
      escaped_argv.append(arg)
    prev_arg = arg
  return escaped_argv


def main():
  try:
    options = config.Options(
        _fix_spaces(_expand_args(sys.argv[1:])), command_line=True
    )
  except utils.UsageError as e:
    print(str(e), file=sys.stderr)
    sys.exit(1)

  if options.show_config:
    print(options)
    sys.exit(0)

  if options.version:
    print(io.get_pytype_version())
    sys.exit(0)

  if options.exec_log:
    with options.open_function(options.exec_log, "w") as f:
      f.write(" ".join(sys.argv))

  # TODO(mdemello): timeout is temporarily unavailable under win32
  if options.timeout is not None and sys.platform != "win32":
    signal.alarm(options.timeout)

  with _ProfileContext(options.profile):
    with metrics.MetricsContext(options.metrics, options.open_function):
      with metrics.StopWatch("total_time"):
        with metrics.Snapshot("memory", enabled=options.memory_snapshots):
          return _run_pytype(options)


def _run_pytype(options):
  """Run pytype with the given configuration options."""
  if options.generate_builtins:
    return _generate_builtins_pickle(options)
  elif options.parse_pyi:
    unused_ast = io.parse_pyi(options)
    return 0
  else:
    return io.process_one_file(options)


if __name__ == "__main__":
  sys.exit(main() or 0)
