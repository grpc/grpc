# NOTE: Do not add any dependencies to this file - it needs to be run in a
# subprocess by a python version that might not have any installed packages,
# including importlab itself.

from __future__ import print_function

import ast
import json
import os
import sys

# Pytype doesn't recognize the `major` attribute:
# https://github.com/google/pytype/issues/127.
if sys.version_info[0] >= 3:
    # Note that `import importlib` does not work: accessing `importlib.util`
    # will give an attribute error. This is hard to reproduce in a unit test but
    # can be seen by installing importlab in a Python 3 environment and running
    # `importlab --tree --trim` on a file that imports one of:
    #   * jsonschema (`pip install jsonschema`)
    #   * pytype (`pip install pytype`),
    #   * dotenv (`pip install python-dotenv`)
    #   * IPython (`pip install ipython`)
    # A correct output will look like:
    #   Reading 1 files
    #   Source tree:
    #   + foo.py
    #       :: jsonschema/__init__.py
    # An incorrect output will be missing the line with the import.
    import importlib.util
else:
    import imp


class ImportFinder(ast.NodeVisitor):
    """Walk an AST collecting import statements."""

    def __init__(self):
        # tuples of (name, alias, is_from, is_star)
        self.imports = []

    def visit_Import(self, node):
        for alias in node.names:
            self.imports.append((alias.name, alias.asname, False, False))

    def visit_ImportFrom(self, node):
        module_name = '.'*node.level + (node.module or '')
        for alias in node.names:
            if alias.name == '*':
                self.imports.append((module_name, alias.asname, True, True))
            else:
                if not module_name.endswith('.'):
                    module_name = module_name + '.'
                name = module_name + alias.name
                asname = alias.asname or alias.name
                self.imports.append((name, asname, True, False))


def _find_package(parts):
    """Helper function for _resolve_import_versioned."""
    for i in range(len(parts), 0, -1):
        prefix = '.'.join(parts[0:i])
        if prefix in sys.modules:
            return i, sys.modules[prefix]
    return 0, None


def is_builtin(name):
    return name in sys.builtin_module_names or name.startswith("__future__")


# Pytype doesn't recognize the `major` attribute:
# https://github.com/google/pytype/issues/127.
if sys.version_info[0] < 3:
    def _resolve_import_versioned(name):
        """Python 2 helper function for resolve_import."""
        parts = name.split('.')
        i, mod = _find_package(parts)
        if mod:
            if hasattr(mod, '__file__'):
                path = [os.path.dirname(mod.__file__)]
            elif hasattr(mod, '__path__'):
                path = mod.__path__
            else:
                path = None
        else:
            path = None
        for part in parts[i:]:
            try:
                if path:
                    spec = imp.find_module(part, [path])
                else:
                    spec = imp.find_module(part)
            except ImportError:
                return None
            path = spec[1]
        return path
else:
    def _resolve_import_versioned(name):
        """Python 3 helper function for resolve_import."""
        try:
            spec = importlib.util.find_spec(name)
            return spec and spec.origin
        except Exception:
            # find_spec may re-raise an arbitrary exception encountered while
            # inspecting a module. Since we aren't able to get the file path in
            # this case, we consider the import unresolved.
            return None


def _resolve_import(name):
    """Helper function for resolve_import."""
    if name in sys.modules:
        return getattr(sys.modules[name], '__file__', name + '.so')
    return _resolve_import_versioned(name)


def resolve_import(name, is_from, is_star):
    """Use python to resolve an import.

    Args:
      name: The fully qualified module name.

    Returns:
      The path to the module source file or None.
    """
    # Don't try to resolve relative imports or builtins here; they will be
    # handled by resolve.Resolver
    if name.startswith('.') or is_builtin(name):
        return None
    ret = _resolve_import(name)
    if ret is None and is_from and not is_star:
        package, _ = name.rsplit('.', 1)
        ret = _resolve_import(package)
    return ret


def get_imports(filename):
    """Get all the imports in a file.

    Each import is a tuple of:
      (name, alias, is_from, is_star, source_file)
    """
    with open(filename, "rb") as f:
        src = f.read()
    finder = ImportFinder()
    finder.visit(ast.parse(src, filename=filename))
    imports = []
    for i in finder.imports:
        name, _, is_from, is_star = i
        imports.append(i + (resolve_import(name, is_from, is_star),))
    return imports


def print_imports(filename):
    """Print imports in csv format to stdout."""
    print(json.dumps(get_imports(filename)))


def read_imports(imports_str):
    """Print imports in csv format to stdout."""
    return json.loads(imports_str)


if __name__ == "__main__":
    # This is used to parse a file with a different python version, launching a
    # subprocess and communicating with it via reading stdout.
    filename = sys.argv[1]
    print_imports(filename)
