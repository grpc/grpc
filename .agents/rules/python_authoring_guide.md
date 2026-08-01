---
trigger: glob
globs: "*.py"
---

<python_authoring_safeguards>

# Python Code Authoring & Formatting Guidelines (Google Style)

Adhere strictly to the [Google Python Style Guide](https://raw.githubusercontent.com/google/styleguide/refs/heads/gh-pages/pyguide.md) across all Python source code modifications:

## 1. Imports & Module Organization

*   **Module & Package Imports**: Import modules and packages exclusively (`import sys`, `from urllib import parse`); prohibit importing individual domain functions or classes directly.
*   **Typing Symbol Exemptions**: Permit importing individual symbols strictly from static analysis modules (`from typing import Any, TextIO`, `from collections.abc import Callable, Iterable`, `from typing_extensions import override, TypeAlias`).
*   **No Relative Imports**: Enforce full absolute package paths (`from grpc.tools import bloat`); prohibit relative imports (`from . import bloat`).

## 2. Code Formatting, Linting & Static Analysis

*   **Mandatory Tool Execution**: Strictly execute configured formatters, linters, and type checkers (e.g., `black`, `ruff`, `pyright`, `isort`) on modified Python source files prior to committing or finalizing changes.
*   **Line Limits & Indentation**: Enforce 80-character line length limit and use 4 spaces per indentation level.
*   **Virtual Environment Tool Execution**: Prefer direct binary invocation (`./.venv/bin/<tool>`) over global paths; avoid subshell chaining (`source .venv/bin/activate`).
*   **Package Management**: Prefer `uv pip` over `./.venv/bin/pip` if `uv` is used or available.
*   **Tool Fallback Strategy**: Verify binary exists in `.venv/bin/` before execution; fallback to global `which <tool>` if absent.
*   **Tool Installation**: Prompt user to install needed tools using `VIRTUAL_ENV=.venv uv pip install <tool>==<version>`.
*   **Version Discovery**: Inspect `tools/distrib/*.sh` (e.g., `tools/distrib/pyright_code.sh`) to determine required tool versions.
*   **Respect Project Tool Settings**: Inspect workspace settings (`.code-workspace`) and project configuration files (`grpc-style-config.toml`, `pyproject.toml`); pass explicit `--config` flags when executing tooling.
*   **Native f-String Formatting**: Prefer f-strings and format specifiers (`f"{msg:#^{width}}"`) over string methods or general `%` formatting.
*   **Lazy Logging Interpolation**: Prohibit f-strings and `.format()` in logging invocations (`logging.info(f"...")`); strictly pass positional `%` format placeholders (`logging.info("Value: %s", val)`).

## 3. System Operations & Subprocess Execution

*   **Prefer `pathlib.Path`**: Prefer `pathlib.Path` for filesystem operations (directory creation, path resolution, globbing) over spawning shell subprocesses (`mkdir`, `rm`) or manual path string manipulation via `os.path`.

## 4. Type Annotations & Parameter Defaults

*   **Modern Specific Type Annotations**: Prefer native PEP 604 unions and built-in generic collections (`str | None`, `list[str]`, `dict[str, int]`) over generic `object` or legacy typing constructs (`Optional[str]`, `List[str]`).
*   **Immutable Static Defaults**: Prohibit binding mutable containers (`{}`, `[]`) as static parameter defaults; strictly assign `None` with lazy in-body initialization (`if items is None: items = []`).
*   **Dynamic Runtime Stream Binding**: Prohibit binding system stream attributes (`sys.stdout`, `sys.stderr`) as static parameter defaults; strictly assign `None` and resolve stream objects dynamically inside function bodies.

## 5. Exception & Cleanup Safety

*   **Catch Specific Exception Types**: Prefer narrow exception types (`subprocess.CalledProcessError`) over broad `Exception` or bare `except:`.
*   **Robust Cleanup Blocks**: Strictly handle secondary errors within `finally` cleanup blocks without overriding primary exceptions or `SystemExit` codes.

## 6. Docstrings & Documentation Syntax

*   **Google Docstring Syntax**: Enforce Google-style docstrings (`Args:`, `Returns:`, `Raises:`, `Yields:`) with 2-space indented descriptions for non-trivial functions; prohibit legacy reStructuredText tags (`:param x:`).
*   **Prohibit Type Duplication**: Prohibit repeating parameter or return types inside docstring descriptions when static type annotations already exist in the function signature.

## 7. Control Flow & Boolean Evaluations

*   **Implicit Boolean Evaluations**: Enforce implicit truth evaluations for containers and strings (`if not items:`, `if name:`); prohibit verbose length or equality comparisons (`if len(items) == 0:`, `if name != "":`).
*   **Singleton Identity Checks**: Strictly evaluate singletons via identity operators (`if obj is None:`); prohibit equality operators (`if obj == None:`).
*   **Comprehension Complexity Limits**: Restrict list, dict, and set comprehensions to a single `for` clause and a single simple `if` condition; convert multi-clause expressions into explicit `for` loops.

## 8. Resource Management & State Architecture

*   **Mandatory Resource Contexts**: Strictly manage stateful resources (files, sockets, transactions) inside explicit context managers (`with open(...) as f:`); prohibit manual `.close()` lifecycle calls.
*   **Prohibit Mutable Global State**: Prohibit defining module-level mutable containers or class attributes that mutate at runtime; encapsulate state within instantiated objects or dedicated caching mechanisms.
*   **Internal Attribute Naming**: Enforce a single leading underscore for internal class attributes and helper methods (`_internal_method()`, `self._cache`); prohibit double-underscore name mangling (`__private`) unless avoiding subclass collisions in third-party frameworks.

</python_authoring_safeguards>
