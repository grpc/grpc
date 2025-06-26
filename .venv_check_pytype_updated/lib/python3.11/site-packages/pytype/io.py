"""Public interface to top-level pytype functions."""

import contextlib
import dataclasses
import logging
import os
import sys
import traceback

import libcst
from pytype import __version__
from pytype import analyze
from pytype import config
from pytype import constant_folding
from pytype import context
from pytype import load_pytd
from pytype import utils
from pytype.directors import directors
from pytype.imports import builtin_stubs as pytd_builtins
from pytype.imports import pickle_utils
from pytype.pyc import pyc
from pytype.pyi import parser
from pytype.pytd import optimize
from pytype.pytd import pytd
from pytype.pytd import pytd_utils
from pytype.pytd import serialize_ast
from pytype.pytd import visitors
from pytype.rewrite import analyze as rewrite_analyze


log = logging.getLogger(__name__)


# Webpage explaining the pytype error codes
ERROR_DOC_URL = "https://google.github.io/pytype/errors.html"


@dataclasses.dataclass
class AnalysisResult:
  """Preserve all state and results from running pytype."""

  context: context.Context
  ast: pytd.TypeDeclUnit | None
  pyi: str | None


def read_source_file(input_filename, open_function=open):
  try:
    with open_function(input_filename, "r", encoding="utf8") as fi:
      return fi.read()
  except OSError as e:
    raise utils.UsageError(f"Could not load input file {input_filename}") from e


def _set_verbosity_from(posarg):
  """Decorator to set the verbosity for a function that takes an options arg.

  Assumes that the function has an argument named `options` that is a
  config.Options object.

  Arguments:
    posarg: The index of `options` in the positional arguments.

  Returns:
    The decorator.
  """

  def decorator(f):
    def wrapper(*args, **kwargs):
      options = kwargs.get("options", args[posarg])
      with config.verbosity_from(options):
        return f(*args, **kwargs)

    return wrapper

  return decorator


@_set_verbosity_from(posarg=2)
def _call(analyze_types, src, options, loader):
  """Helper function to call analyze.check/infer_types."""
  loader = loader or load_pytd.create_loader(options)
  return analyze_types(src=src, options=options, loader=loader)


def check_py(src, options=None, loader=None):
  """Check the types of a string of source code."""
  options = options or config.Options.create()
  if options.use_rewrite:
    check_types = rewrite_analyze.check_types
  else:
    check_types = analyze.check_types
  with config.verbosity_from(options):
    return _call(check_types, src, options, loader)


def generate_pyi_ast(
    src: str,
    options: config.Options | None = None,
    loader: load_pytd.Loader | None = None,
) -> analyze.Analysis:
  """Run the inferencer on a string of source code, producing output.

  Args:
    src: The source code.
    options: config.Options object.
    loader: A load_pytd.Loader instance.

  Returns:
    An analyze.Analysis object containing the inferencer results.

  Raises:
    CompileError: If we couldn't parse the input file.
    UsageError: If the input filepath is invalid.
  """
  options = options or config.Options.create()
  if options.use_rewrite:
    infer_types = rewrite_analyze.infer_types
  else:
    infer_types = analyze.infer_types
  with config.verbosity_from(options):
    ret = _call(infer_types, src, options, loader)
    mod = ret.ast
    mod.Visit(visitors.VerifyVisitor())
    mod = optimize.Optimize(
        mod,
        ret.ast_deps,
        lossy=False,
        use_abcs=False,
        max_union=7,
        remove_mutable=False,
    )
    mod = pytd_utils.CanonicalOrdering(mod)
  ret.ast = mod
  return ret


def _output_ast(
    ast: pytd.TypeDeclUnit,
    options: config.Options | None = None,
) -> str:
  """Transforms the given analysis result into a pyi representation.

  Args:
    ast: pytd.TypeDeclUnit to output in pyi format.
    options: config.Options object.

  Returns:
    A pyi representation of the given AST as a string.
  """
  options = options or config.Options.create()
  with config.verbosity_from(options):
    result = pytd_utils.Print(ast)
    log.info("=========== pyi optimized =============")
    log.info("\n%s", result)
    log.info("========================================")

  result += "\n"
  if options.quick:
    result = "# (generated with --quick)\n\n" + result
  return result


def generate_pyi(src, options=None, loader=None):
  """Run the inferencer on a string of source code, producing output.

  Args:
    src: The source code.
    options: config.Options object.
    loader: A load_pytd.Loader instance.

  Returns:
    A tuple, (analyze.Analysis, pyi ast as string).

  Raises:
    CompileError: If we couldn't parse the input file.
    UsageError: If the input filepath is invalid.
  """
  options = options or config.Options.create()
  ret = generate_pyi_ast(src, options, loader)
  return ret, _output_ast(ret.ast, options)


@_set_verbosity_from(posarg=0)
def check_or_generate_pyi(options) -> AnalysisResult:
  """Returns results from running pytype.

  Args:
    options: config.Options object.

  Returns:
    An AnalysisResult.
  """
  loader = load_pytd.create_loader(options)
  compiler_error = None
  other_error_info = ""
  src = ""
  try:
    src = read_source_file(options.input, options.open_function)
    if options.check:
      ctx = check_py(src=src, options=options, loader=loader).context
      ast, result = None, None
    else:
      ret, result = generate_pyi(src=src, options=options, loader=loader)
      ctx = ret.context
      ast = ret.ast
  except utils.UsageError:
    raise
  except pyc.CompileError as e:
    compiler_error = (options.input, e.line, e.error)
  except constant_folding.ConstantError as e:
    compiler_error = (options.input, e.lineno, e.message)
  except IndentationError as e:
    compiler_error = (options.input, e.lineno, e.msg)
  except libcst.ParserSyntaxError as e:
    # TODO(rechen): We can get rid of this branch once we delete
    # directors.parser_libcst.
    compiler_error = (options.input, e.raw_line, e.message)
  except SyntaxError as e:
    compiler_error = (options.input, e.lineno, e.msg)
  except directors.SkipFileError:
    other_error_info = "# skip-file found, file not analyzed"
  except Exception as e:  # pylint: disable=broad-except
    if options.nofail:
      log.warning("***Caught exception: %s", str(e), exc_info=True)
      if not options.check:
        other_error_info = (
            "# Caught error in pytype: "
            + str(e).replace("\n", "\n#")
            + "\n# "
            + "\n# ".join(traceback.format_exc().splitlines())
        )
    else:
      prefix = str(e.args[0]) if e.args else ""
      e.args = (f"{prefix}\nFile: {options.input}",) + e.args[1:]
      raise
  else:
    return AnalysisResult(ctx, ast, result)

  # check_py/generate_pyi has raised an exception.
  ctx = context.Context(options, loader, src=src)
  if compiler_error:
    ctx.errorlog.python_compiler_error(*compiler_error)
  ast = pytd_builtins.GetDefaultAst(
      parser.PyiOptions.from_toplevel_options(options)
  )
  result = pytd_builtins.DEFAULT_SRC + other_error_info
  return AnalysisResult(ctx, ast, result)


def _write_pyi_output(options, contents, filename):
  assert filename
  if filename == "-":
    sys.stdout.write(contents)
  else:
    log.info("write pyi %r => %r", options.input, filename)
    with options.open_function(filename, "w") as fi:
      fi.write(contents)


@_set_verbosity_from(posarg=0)
def process_one_file(options):
  """Check a .py file or generate a .pyi for it, according to options.

  Args:
    options: config.Options object.

  Returns:
    An error code (0 means no error).
  """

  log.info("Process %s => %s", options.input, options.output)
  try:
    ret = check_or_generate_pyi(options)
  except utils.UsageError:
    logging.exception("")
    return 1

  if not options.check:
    if options.pickle_output:
      pyi_output = options.verify_pickle
    else:
      pyi_output = options.output
    # Write out the pyi file.
    if pyi_output:
      _write_pyi_output(options, ret.pyi, pyi_output)
    # Write out the pickle file.
    if options.pickle_output:
      log.info("write pickle %r => %r", options.input, options.output)
      write_pickle(ret.ast, options, ret.context.loader)

  if options.unused_imports_info_files:
    if options.use_rewrite:
      pass  # not implemented yet
    else:
      # Sort the paths to make the output stable.
      cwd = os.getcwd()
      unused_paths = sorted(ret.context.loader.get_unused_imports_map_paths())
      with options.open_function(
          options.unused_imports_info_files, "wt", encoding="utf-8"
      ) as f:
        for unused_path in unused_paths:
          f.write(f"{os.path.relpath(unused_path, cwd)}\n")
  exit_status = handle_errors(ret.context.errorlog, options)

  # Give the garbage collector a little help.
  # Without this line, successive calls to this method, such as in a long-lived
  # persistent worker, cause memory usage to grow seemingly without bound. With
  # this line, memory usage remains stable. Do not remove before first checking
  # that this is no longer needed.
  # TODO(b/350414460): Get to the bottom of why this is needed.
  ret.context.program = None

  # Touch output file upon success.
  if options.touch and not exit_status:
    with options.open_function(options.touch, "a"):
      os.utime(options.touch, None)
  return exit_status


@_set_verbosity_from(posarg=1)
def write_pickle(ast, options, loader=None):
  """Dump a pickle of the ast to a file."""
  loader = loader or load_pytd.create_loader(options)
  try:
    ast = serialize_ast.PrepareForExport(options.module_name, ast, loader)
  except parser.ParseError as e:
    if options.nofail:
      ast = serialize_ast.PrepareForExport(
          options.module_name, loader.get_default_ast(), loader
      )
      log.warning("***Caught exception: %s", str(e), exc_info=True)
    else:
      raise
  if options.verify_pickle:
    ast1 = ast.Visit(visitors.LateTypeToClassType())
    ast1 = ast1.Visit(visitors.ClearClassPointers())
    ast2 = loader.load_file(options.module_name, options.verify_pickle)
    ast2 = ast2.Visit(visitors.ClearClassPointers())
    if not pytd_utils.ASTeq(ast1, ast2):
      raise AssertionError()
  pickle_utils.SerializeAndSave(
      ast,
      filename=options.output,
      src_path=options.input,
      metadata=options.pickle_metadata,
      open_function=options.open_function,
  )


def print_error_doc_url(errorlog):
  names = {e.name for e in errorlog}
  if names:
    doclink = f"\nFor more details, see {ERROR_DOC_URL}"
    if len(names) == 1:
      doclink += "#" + names.pop()
    print(doclink, file=sys.stderr)


@_set_verbosity_from(posarg=1)
def handle_errors(errorlog, options):
  """Handle the errorlog according to the given options."""
  if not options.report_errors:
    return 0

  if options.output_errors_csv:
    with options.open_function(options.output_errors_csv, "w") as f:
      errorlog.print_to_csv_file(f)

  errorlog.print_to_stderr(color=options.color)
  print_error_doc_url(errorlog)

  # exit code
  return 1 if errorlog.has_error() and not options.return_success else 0


@_set_verbosity_from(posarg=0)
def parse_pyi(options):
  """Tries parsing a PYI file."""
  loader = load_pytd.create_loader(options)
  ast = loader.load_file(options.module_name, options.input)
  ast = loader.finish_and_verify_ast(ast)
  if options.output:
    result = "# Internal AST parsed and postprocessed from {}\n\n{}".format(
        options.input, pytd_utils.Print(ast)
    )
    _write_pyi_output(options, result, options.output)
  return ast


def get_pytype_version():
  return __version__.__version__


@contextlib.contextmanager
def wrap_pytype_exceptions(exception_type, filename=""):
  """Catch pytype errors and reraise them as a single exception type.

  NOTE: This will also wrap non-pytype errors thrown within the body of the
  code block; it is therefore recommended to use this to wrap a single function
  call.

  Args:
    exception_type: The class to wrap exceptions in.
    filename: A filename to use in error messages.

  Yields:
    nothing, just calls the code block.
  """
  try:
    yield
  except utils.UsageError as e:
    raise exception_type(f"Pytype usage error: {e}") from e
  except pyc.CompileError as e:
    raise exception_type(
        "Error reading file %s at line %s: %s" % (filename, e.line, e.error)
    ) from e
  except libcst.ParserSyntaxError as e:
    # TODO(rechen): We can get rid of this branch once we delete
    # directors.parser_libcst.
    raise exception_type(
        "Error reading file %s at line %s: %s"
        % (filename, e.raw_line, e.message)
    ) from e
  except SyntaxError as e:
    raise exception_type(
        "Error reading file %s at line %s: %s" % (filename, e.lineno, e.msg)
    ) from e
  except directors.SkipFileError as e:
    raise exception_type(
        "Pytype could not analyze file %s: '# skip-file' directive found"
        % filename
    ) from e
  except pickle_utils.LoadPickleError as e:
    raise exception_type(
        f"Error analyzing file {filename}: Could not load "
        f"serialized dependency {e.filename}"
    ) from e
  except Exception as e:  # pylint: disable=broad-except
    msg = f"Pytype error: {e.__class__.__name__}: {e.args[0]}"
    raise exception_type(msg).with_traceback(e.__traceback__)
