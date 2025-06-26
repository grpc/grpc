"""Analyze an entire project using pytype."""

import logging
import sys

import importlab.environment
import importlab.fs
import importlab.graph
import importlab.output

from pytype import io
from pytype.platform_utils import path_utils
from pytype.platform_utils import tempfile as compatible_tempfile
from pytype.tools import environment
from pytype.tools import tool_utils
from pytype.tools.analyze_project import config
from pytype.tools.analyze_project import environment as analyze_project_env
from pytype.tools.analyze_project import parse_args
from pytype.tools.analyze_project import pytype_runner


def main():
  parser = parse_args.make_parser()
  args = parser.parse_args(sys.argv[1:])

  if args.version:
    print(io.get_pytype_version())
    sys.exit(0)

  tool_utils.setup_logging_or_die(args.verbosity)

  if args.generate_config:
    config.generate_sample_config_or_die(args.generate_config,
                                         parser.pytype_single_args)
    sys.exit(0)

  conf = parser.config_from_defaults()
  # File options overwrite defaults.
  file_config = config.read_config_file_or_die(args.config)
  parser.convert_strings(file_config)
  parser.postprocess(file_config)
  conf.populate_from(file_config)
  # Command line arguments overwrite file options.
  conf.populate_from(args)
  conf.inputs -= conf.exclude
  if args.no_cache:
    conf.output = compatible_tempfile.mkdtemp()
  if not conf.pythonpath:
    conf.pythonpath = environment.compute_pythonpath(conf.inputs)
  logging.info('\n  '.join(['Configuration:'] + str(conf).split('\n')))

  if not conf.inputs:
    parser.error('Need an input.')

  # Importlab needs the python exe, so we check it as early as possible.
  environment.check_python_exe_or_die(conf.python_version)

  typeshed = environment.initialize_typeshed_or_die()
  env = analyze_project_env.create_importlab_environment(conf, typeshed)
  print('Computing dependencies')
  import_graph = importlab.graph.ImportGraph.create(env, conf.inputs, trim=True)

  if args.tree:
    print('Source tree:')
    importlab.output.print_tree(import_graph)
    sys.exit(0)

  if args.unresolved:
    print('Unresolved dependencies:')
    for imp in sorted(import_graph.get_all_unresolved()):
      print(' ', imp.name)
    sys.exit(0)

  # Main usage mode: analyze the project file by file in dependency order.

  logging.info('Source tree:\n%s',
               importlab.output.formatted_deps_list(import_graph))
  tool_utils.makedirs_or_die(conf.output, 'Could not create output directory')
  with open(path_utils.join(conf.output, '.gitignore'), 'w') as f:
    f.write('# Automatically created by pytype\n*')
  deps = pytype_runner.deps_from_import_graph(import_graph)
  runner = pytype_runner.PytypeRunner(conf, deps)
  return runner.run()


if __name__ == '__main__':
  sys.exit(main())
