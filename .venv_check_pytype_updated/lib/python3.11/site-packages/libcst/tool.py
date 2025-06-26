# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Usage:
#
# python -m libcst.tool --help
# python -m libcst.tool print python_file.py

import argparse
import importlib
import inspect
import os
import os.path
import shutil
import sys
import textwrap
from abc import ABC, abstractmethod
from typing import Any, Callable, Dict, List, Tuple, Type

try:
    import yaml_ft as yaml  # pyre-ignore
except ModuleNotFoundError:
    import yaml

from libcst import CSTLogicError, LIBCST_VERSION, parse_module, PartialParserConfig
from libcst._parser.parso.utils import parse_version_string
from libcst.codemod import (
    CodemodCommand,
    CodemodContext,
    diff_code,
    exec_transform_with_prettyprint,
    gather_files,
    parallel_exec_transform_with_prettyprint,
)
from libcst.display import dump, dump_graphviz
from libcst.display.text import _DEFAULT_INDENT


def _print_tree_impl(proc_name: str, command_args: List[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Print the LibCST tree representation of a file.",
        prog=f"{proc_name} print",
        fromfile_prefix_chars="@",
    )
    parser.add_argument(
        "infile",
        metavar="INFILE",
        help='File to print tree for. Use "-" for stdin',
        type=str,
    )
    parser.add_argument(
        "--show-whitespace",
        action="store_true",
        help="Show whitespace nodes in printed tree",
    )
    parser.add_argument(
        "--show-defaults",
        action="store_true",
        help="Show values that are unchanged from the default",
    )
    parser.add_argument(
        "--show-syntax",
        action="store_true",
        help="Show values that exist only for syntax, like commas or semicolons",
    )
    parser.add_argument(
        "--graphviz",
        action="store_true",
        help="Displays the graph in .dot format, compatible with Graphviz",
    )
    parser.add_argument(
        "--indent-string",
        default=_DEFAULT_INDENT,
        help=f"String to use for indenting levels, defaults to {_DEFAULT_INDENT!r}",
    )
    parser.add_argument(
        "-p",
        "--python-version",
        metavar="VERSION",
        help=(
            "Override the version string used for parsing Python source files. Defaults "
            + "to the version of python used to run this tool."
        ),
        type=str,
        default=None,
    )
    args = parser.parse_args(command_args)
    infile = args.infile

    # Grab input file
    if infile == "-":
        code = sys.stdin.read()
    else:
        with open(infile, "rb") as fp:
            code = fp.read()

    tree = parse_module(
        code,
        config=(
            PartialParserConfig(python_version=args.python_version)
            if args.python_version is not None
            else PartialParserConfig()
        ),
    )
    if not args.graphviz:
        print(
            dump(
                tree,
                indent=args.indent_string,
                show_defaults=args.show_defaults,
                show_syntax=args.show_syntax,
                show_whitespace=args.show_whitespace,
            )
        )
    else:
        print(
            dump_graphviz(
                tree,
                show_defaults=args.show_defaults,
                show_syntax=args.show_syntax,
                show_whitespace=args.show_whitespace,
            )
        )
    return 0


def _default_config() -> Dict[str, Any]:
    return {
        "generated_code_marker": f"@gen{''}erated",
        "formatter": ["black", "-"],
        "blacklist_patterns": [],
        "modules": ["libcst.codemod.commands"],
        "repo_root": ".",
    }


CONFIG_FILE_NAME = ".libcst.codemod.yaml"


def _find_and_load_config(proc_name: str) -> Dict[str, Any]:
    # Initialize with some sane defaults.
    config = _default_config()

    # Walk up the filesystem looking for a config file.
    current_dir = os.path.abspath(os.getcwd())
    previous_dir = None
    found_config = False
    while current_dir != previous_dir:
        # See if the config file exists
        config_file = os.path.join(current_dir, CONFIG_FILE_NAME)
        if os.path.isfile(config_file):
            # Load it, override defaults with what is in the config.
            with open(config_file, "r") as fp:
                possible_config = yaml.safe_load(fp.read())

            # Lets be careful with all user input so we don't crash.
            if isinstance(possible_config, dict):
                # Grab the generated code marker.
                for str_setting in ["generated_code_marker"]:
                    if str_setting in possible_config and isinstance(
                        possible_config[str_setting], str
                    ):
                        config[str_setting] = possible_config[str_setting]

                # Grab the formatter, blacklisted patterns and module directories.
                for list_setting in ["formatter", "blacklist_patterns", "modules"]:
                    if (
                        list_setting in possible_config
                        and isinstance(possible_config[list_setting], list)
                        and all(
                            isinstance(s, str) for s in possible_config[list_setting]
                        )
                    ):
                        config[list_setting] = possible_config[list_setting]

                # Grab the repo root config.
                for path_setting in ["repo_root"]:
                    if path_setting in possible_config and isinstance(
                        possible_config[path_setting], str
                    ):
                        config[path_setting] = os.path.abspath(
                            os.path.join(current_dir, possible_config[path_setting]),
                        )

                # We successfully located a file, stop traversing.
                found_config = True
                break

        # Try the parent directory.
        previous_dir = current_dir
        current_dir = os.path.abspath(os.path.join(current_dir, os.pardir))

    requires_config = bool(os.environ.get("LIBCST_TOOL_REQUIRE_CONFIG", ""))
    if requires_config and not found_config:
        raise FileNotFoundError(
            f"Did not find a {CONFIG_FILE_NAME} in current directory or any "
            + "parent directory! Perhaps you meant to run this command from a "
            + "configured subdirectory, or you need to initialize a new project "
            + f'using "{proc_name} initialize"?'
        )

    # Make sure that the formatter is findable.
    if config["formatter"]:
        exe = shutil.which(config["formatter"][0]) or config["formatter"][0]
        config["formatter"] = [os.path.abspath(exe), *config["formatter"][1:]]

    return config


def _codemod_impl(proc_name: str, command_args: List[str]) -> int:  # noqa: C901
    # Grab the configuration for running this, if it exsts.
    config = _find_and_load_config(proc_name)

    # First, try to grab the command with a first pass. We aren't going to react
    # to user input here, so refuse to add help. Help will be parsed in the
    # full parser below once we know the command and have added its arguments.
    parser = argparse.ArgumentParser(add_help=False, fromfile_prefix_chars="@")
    parser.add_argument("command", metavar="COMMAND", type=str, nargs="?", default=None)
    ext_action = parser.add_argument(
        "-x",
        "--external",
        action="store_true",
        default=False,
        help="Interpret `command` as just a module/class specifier",
    )
    args, _ = parser.parse_known_args(command_args)

    # Now, try to load the class and get its arguments for help purposes.
    if args.command is not None:
        command_module_name, _, command_class_name = args.command.rpartition(".")
        if not (command_module_name and command_class_name):
            print(f"{args.command} is not a valid codemod command", file=sys.stderr)
            return 1
        if args.external:
            # There's no error handling here on purpose; if the user opted in for `-x`,
            # they'll probably want to see the exact import error too.
            command_class = getattr(
                importlib.import_module(command_module_name),
                command_class_name,
            )
        else:
            command_class = None
            for module in config["modules"]:
                try:
                    command_class = getattr(
                        importlib.import_module(f"{module}.{command_module_name}"),
                        command_class_name,
                    )
                    break
                # Only swallow known import errors, show the rest of the exceptions
                # to the user who is trying to run the codemod.
                except AttributeError:
                    continue
                except ModuleNotFoundError:
                    continue
            if command_class is None:
                print(
                    f"Could not find {command_module_name} in any configured modules",
                    file=sys.stderr,
                )
                return 1
    else:
        # Dummy, specifically to allow for running --help with no arguments.
        command_class = CodemodCommand

    # Now, construct the full parser, parse the args and run the class.
    parser = argparse.ArgumentParser(
        description=(
            "Execute a codemod against a series of files."
            if command_class is CodemodCommand
            else command_class.DESCRIPTION
        ),
        prog=f"{proc_name} codemod",
        fromfile_prefix_chars="@",
    )
    parser._add_action(ext_action)
    parser.add_argument(
        "command",
        metavar="COMMAND",
        type=str,
        help=(
            "The name of the file (minus the path and extension) and class joined with "
            + "a '.' that defines your command (e.g. strip_strings_from_types.StripStringsCommand)"
        ),
    )
    parser.add_argument(
        "path",
        metavar="PATH",
        nargs="+",
        help=(
            "Path to codemod. Can be a directory, file, or multiple of either. To "
            + 'instead read from stdin and write to stdout, use "-"'
        ),
    )
    parser.add_argument(
        "-j",
        "--jobs",
        metavar="JOBS",
        help="Number of jobs to use when processing files. Defaults to number of cores",
        type=int,
        default=None,
    )
    parser.add_argument(
        "-p",
        "--python-version",
        metavar="VERSION",
        help=(
            "Override the version string used for parsing Python source files. Defaults "
            + "to the version of python used to run this tool."
        ),
        type=str,
        default=None,
    )
    parser.add_argument(
        "-u",
        "--unified-diff",
        metavar="CONTEXT",
        help="Output unified diff instead of contents. Implies outputting to stdout",
        type=int,
        nargs="?",
        default=None,
        const=5,
    )
    parser.add_argument(
        "--include-generated", action="store_true", help="Codemod generated files."
    )
    parser.add_argument(
        "--include-stubs", action="store_true", help="Codemod typing stub files."
    )
    parser.add_argument(
        "--no-format",
        action="store_true",
        help="Don't format resulting codemod with configured formatter.",
    )
    parser.add_argument(
        "--show-successes",
        action="store_true",
        help="Print files successfully codemodded with no warnings.",
    )
    parser.add_argument(
        "--hide-generated-warnings",
        action="store_true",
        help="Do not print files that are skipped for being autogenerated.",
    )
    parser.add_argument(
        "--hide-blacklisted-warnings",
        action="store_true",
        help="Do not print files that are skipped for being blacklisted.",
    )
    parser.add_argument(
        "--hide-progress",
        action="store_true",
        help="Do not print progress indicator. Useful if calling from a script.",
    )
    command_class.add_args(parser)
    args = parser.parse_args(command_args)

    codemod_args = {
        k: v
        for k, v in vars(args).items()
        if k
        not in {
            "command",
            "external",
            "hide_blacklisted_warnings",
            "hide_generated_warnings",
            "hide_progress",
            "include_generated",
            "include_stubs",
            "jobs",
            "no_format",
            "path",
            "python_version",
            "show_successes",
            "unified_diff",
        }
    }
    # Sepcify target version for black formatter
    if any(config["formatter"]) and os.path.basename(config["formatter"][0]) in (
        "black",
        "black.exe",
    ):
        parsed_version = parse_version_string(args.python_version)

        config["formatter"] = [
            config["formatter"][0],
            "--target-version",
            f"py{parsed_version.major}{parsed_version.minor}",
        ] + config["formatter"][1:]

    # Special case for allowing stdin/stdout. Note that this does not allow for
    # full-repo metadata since there is no path.
    if any(p == "-" for p in args.path):
        if len(args.path) > 1:
            raise ValueError("Cannot specify multiple paths when reading from stdin!")

        print("Codemodding from stdin", file=sys.stderr)
        oldcode = sys.stdin.read()
        newcode = exec_transform_with_prettyprint(
            command_class(CodemodContext(), **codemod_args),  # type: ignore
            oldcode,
            include_generated=args.include_generated,
            generated_code_marker=config["generated_code_marker"],
            format_code=not args.no_format,
            formatter_args=config["formatter"],
            python_version=args.python_version,
        )
        if not newcode:
            print("Failed to codemod from stdin", file=sys.stderr)
            return 1

        # Now, either print or diff the code
        if args.unified_diff:
            print(diff_code(oldcode, newcode, args.unified_diff, filename="stdin"))
        else:
            print(newcode)
        return 0

    # Let's run it!
    files = gather_files(args.path, include_stubs=args.include_stubs)
    try:
        result = parallel_exec_transform_with_prettyprint(
            command_class,
            files,
            jobs=args.jobs,
            unified_diff=args.unified_diff,
            include_generated=args.include_generated,
            generated_code_marker=config["generated_code_marker"],
            format_code=not args.no_format,
            formatter_args=config["formatter"],
            show_successes=args.show_successes,
            hide_generated=args.hide_generated_warnings,
            hide_blacklisted=args.hide_blacklisted_warnings,
            hide_progress=args.hide_progress,
            blacklist_patterns=config["blacklist_patterns"],
            python_version=args.python_version,
            repo_root=config["repo_root"],
            codemod_args=codemod_args,
        )
    except KeyboardInterrupt:
        print("Interrupted!", file=sys.stderr)
        return 2

    # Print a fancy summary at the end.
    print(
        f"Finished codemodding {result.successes + result.skips + result.failures} files!",
        file=sys.stderr,
    )
    print(f" - Transformed {result.successes} files successfully.", file=sys.stderr)
    print(f" - Skipped {result.skips} files.", file=sys.stderr)
    print(f" - Failed to codemod {result.failures} files.", file=sys.stderr)
    print(f" - {result.warnings} warnings were generated.", file=sys.stderr)
    return 1 if result.failures > 0 else 0


class _SerializerBase(ABC):
    def __init__(self, comment: str) -> None:
        self.comment = comment

    def serialize(self, key: str, value: object) -> str:
        comments = os.linesep.join(
            f"# {comment}" for comment in textwrap.wrap(self.comment)
        )
        return f"{comments}{os.linesep}{self._serialize_impl(key, value)}{os.linesep}"

    @abstractmethod
    def _serialize_impl(self, key: str, value: object) -> str: ...


class _StrSerializer(_SerializerBase):
    def _serialize_impl(self, key: str, value: object) -> str:
        return f"{key}: {value!r}"


class _ListSerializer(_SerializerBase):
    def __init__(self, comment: str, *, newlines: bool = False) -> None:
        super().__init__(comment)
        self.newlines = newlines

    def _serialize_impl(self, key: str, value: object) -> str:
        if not isinstance(value, list):
            raise ValueError("Can only serialize lists!")
        if self.newlines:
            values = [f"- {v!r}" for v in value]
            return f"{key}:{os.linesep}{os.linesep.join(values)}"
        else:
            values = [repr(v) for v in value]
            return f"{key}: [{', '.join(values)}]"


def _initialize_impl(proc_name: str, command_args: List[str]) -> int:
    # Now, construct the full parser, parse the args and run the class.
    parser = argparse.ArgumentParser(
        description="Initialize a directory by writing a default LibCST config to it.",
        prog=f"{proc_name} initialize",
        fromfile_prefix_chars="@",
    )
    parser.add_argument(
        "path",
        metavar="PATH",
        type=str,
        help="Path to initialize with a default LibCST codemod configuration",
    )
    args = parser.parse_args(command_args)

    # Get default configuration file, write it to the YAML file we
    # recognize as our config.
    default_config = _default_config()

    # We serialize for ourselves here, since PyYAML doesn't allow
    # us to control comments in the default file.
    serializers: Dict[str, _SerializerBase] = {
        "generated_code_marker": _StrSerializer(
            "String that LibCST should look for in code which indicates "
            + "that the module is generated code."
        ),
        "formatter": _ListSerializer(
            "Command line and arguments for invoking a code formatter. "
            + "Anything specified here must be capable of taking code via "
            + "stdin and returning formatted code via stdout."
        ),
        "blacklist_patterns": _ListSerializer(
            "List of regex patterns which LibCST will evaluate against "
            + "filenames to determine if the module should be touched."
        ),
        "modules": _ListSerializer(
            "List of modules that contain codemods inside of them.", newlines=True
        ),
        "repo_root": _StrSerializer(
            "Absolute or relative path of the repository root, used for "
            + "providing full-repo metadata. Relative paths should be "
            + "specified with this file location as the base."
        ),
    }

    config_str = "".join(
        serializers[key].serialize(key, val) for key, val in default_config.items()
    )

    # For safety, verify that it parses to the identical file.
    actual_config = yaml.safe_load(config_str)
    if actual_config != default_config:
        raise CSTLogicError("Logic error, serialization is invalid!")

    config_file = os.path.abspath(os.path.join(args.path, CONFIG_FILE_NAME))
    with open(config_file, "w") as fp:
        fp.write(config_str)

    print(f"Successfully wrote default config file to {config_file}")
    return 0


def _recursive_find(base_dir: str, base_module: str) -> List[Tuple[str, object]]:
    """
    Given a base directory and a base module, recursively walk the directory looking
    for importable python modules, returning them and their relative module name
    based off of the base_module.
    """

    modules: List[Tuple[str, object]] = []

    for path in os.listdir(base_dir):
        full_path = os.path.join(base_dir, path)
        if os.path.isdir(full_path):
            # Recursively add files in subdirectories.
            additions = _recursive_find(full_path, f"{base_module}.{path}")
            for module_name, module_object in additions:
                modules.append((f"{path}.{module_name}", module_object))
            continue

        if not os.path.isfile(full_path) or not path.endswith(".py"):
            continue
        try:
            module_name = path[:-3]
            potential_codemod = importlib.import_module(f"{base_module}.{module_name}")
            modules.append((module_name, potential_codemod))
        except Exception:
            # Unlike running a codemod, listing shouldn't crash with exceptions.
            continue

    return modules


def _list_impl(proc_name: str, command_args: List[str]) -> int:  # noqa: C901
    # Grab the configuration so we can determine which modules to list from
    config = _find_and_load_config(proc_name)

    parser = argparse.ArgumentParser(
        description="List all codemods available to run.",
        prog=f"{proc_name} list",
        fromfile_prefix_chars="@",
    )
    _ = parser.parse_args(command_args)

    # Now, import each of the modules to determine their paths.
    codemods: Dict[Type[CodemodCommand], str] = {}
    for module in config["modules"]:
        try:
            imported_module = importlib.import_module(module)
        except Exception:
            # Unlike running a codemod, listing shouldn't crash with exceptions.
            imported_module = None

        if not imported_module:
            print(
                f"Could not import {module}, cannot list codemods inside it",
                file=sys.stderr,
            )
            continue

        # Grab the path, try to import all of the files inside of it.
        # pyre-fixme[6]: For 1st argument expected `PathLike[Variable[AnyStr <:
        #  [str, bytes]]]` but got `Optional[str]`.
        path = os.path.dirname(os.path.abspath(imported_module.__file__))
        for name, imported_module in _recursive_find(path, module):
            for objname in dir(imported_module):
                try:
                    obj = getattr(imported_module, objname)
                    if not issubclass(obj, CodemodCommand):
                        continue
                    if inspect.isabstract(obj):
                        continue
                    # isabstract is broken for direct subclasses of ABC which
                    # don't themselves define any abstract methods, so lets
                    # check for that here.
                    if any(cls[0] is ABC for cls in inspect.getclasstree([obj])):
                        continue
                    # Deduplicate any codemods that were referenced in other
                    # codemods. Always take the shortest name.
                    fullname = f"{name}.{obj.__name__}"
                    if obj in codemods:
                        if len(fullname) < len(codemods[obj]):
                            codemods[obj] = fullname
                    else:
                        codemods[obj] = fullname
                except TypeError:
                    continue

    printable_codemods: List[str] = [
        f"{name} - {obj.DESCRIPTION}" for obj, name in codemods.items()
    ]
    print("\n".join(sorted(printable_codemods)))
    return 0


def main(proc_name: str, cli_args: List[str]) -> int:
    # Hack to allow "--help" to print out generic help, but also allow subcommands
    # to customize their parsing and help messages.
    first_arg = cli_args[0] if cli_args else "--help"
    add_help = first_arg in {"--help", "-h"}

    # Create general parser to determine which command we are invoking.
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Collection of utilities that ship with LibCST.",
        add_help=add_help,
        prog=proc_name,
        fromfile_prefix_chars="@",
    )
    parser.add_argument(
        "--version",
        help="Print current version of LibCST toolset.",
        action="version",
        version=f"LibCST version {LIBCST_VERSION}",  # pyre-ignore[16] pyre bug?
    )
    parser.add_argument(
        "action",
        help="Action to take. Valid options include: print, codemod, list, initialize.",
        choices=["print", "codemod", "list", "initialize"],
    )
    args, command_args = parser.parse_known_args(cli_args)

    # Create a dummy command in case the user manages to get into
    # this state.
    def _invalid_command(proc_name: str, command_args: List[str]) -> int:
        print("Please specify a command!\n", file=sys.stderr)
        parser.print_help(sys.stderr)
        return 1

    # Look up the command and delegate parsing/running.
    lookup: Dict[str, Callable[[str, List[str]], int]] = {
        "print": _print_tree_impl,
        "codemod": _codemod_impl,
        "initialize": _initialize_impl,
        "list": _list_impl,
    }
    return lookup.get(args.action or None, _invalid_command)(proc_name, command_args)


if __name__ == "__main__":
    sys.exit(
        main(os.environ.get("LIBCST_TOOL_COMMAND_NAME", "libcst.tool"), sys.argv[1:])
    )
