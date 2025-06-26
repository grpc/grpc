# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
"""
Provides helpers for CLI interaction.
"""

import difflib
import functools
import os.path
import re
import subprocess
import sys
import time
import traceback
from concurrent.futures import as_completed, Executor
from copy import deepcopy
from dataclasses import dataclass
from multiprocessing import cpu_count
from pathlib import Path
from typing import AnyStr, Callable, cast, Dict, List, Optional, Sequence, Type, Union
from warnings import warn

from libcst import parse_module, PartialParserConfig
from libcst.codemod._codemod import Codemod
from libcst.codemod._context import CodemodContext
from libcst.codemod._dummy_pool import DummyExecutor
from libcst.codemod._runner import (
    SkipFile,
    SkipReason,
    transform_module,
    TransformExit,
    TransformFailure,
    TransformResult,
    TransformSkip,
    TransformSuccess,
)
from libcst.helpers import calculate_module_and_package
from libcst.metadata import FullRepoManager

_DEFAULT_GENERATED_CODE_MARKER: str = f"@gen{''}erated"


def invoke_formatter(formatter_args: Sequence[str], code: AnyStr) -> AnyStr:
    """
    Given a code string, run an external formatter on the code and return new
    formatted code.
    """

    # Make sure there is something to run
    if len(formatter_args) == 0:
        raise ValueError("No formatter configured but code formatting requested.")

    # Invoke the formatter, giving it the code as stdin and assuming the formatted
    # code comes from stdout.
    work_with_bytes = isinstance(code, bytes)
    return cast(
        AnyStr,
        subprocess.check_output(
            formatter_args,
            input=code,
            universal_newlines=not work_with_bytes,
            encoding=None if work_with_bytes else "utf-8",
        ),
    )


def print_execution_result(result: TransformResult) -> None:
    for warning in result.warning_messages:
        print(f"WARNING: {warning}", file=sys.stderr)

    if isinstance(result, TransformFailure):
        error = result.error
        if isinstance(error, subprocess.CalledProcessError):
            print(error.output.decode("utf-8"), file=sys.stderr)
        print(result.traceback_str, file=sys.stderr)


def gather_files(
    files_or_dirs: Sequence[str], *, include_stubs: bool = False
) -> List[str]:
    """
    Given a list of files or directories (can be intermingled), return a list of
    all python files that exist at those locations. If ``include_stubs`` is ``True``,
    this will include ``.py`` and ``.pyi`` stub files. If it is ``False``, only
    ``.py`` files will be included in the returned list.
    """
    ret: List[str] = []
    for fd in files_or_dirs:
        if os.path.isfile(fd):
            ret.append(fd)
        elif os.path.isdir(fd):
            ret.extend(
                str(p)
                for p in Path(fd).rglob("*.py*")
                if Path.is_file(p)
                and (
                    str(p).endswith("py") or (include_stubs and str(p).endswith("pyi"))
                )
            )
    return sorted(ret)


def diff_code(
    oldcode: str, newcode: str, context: int, *, filename: Optional[str] = None
) -> str:
    """
    Given two strings representing a module before and after a codemod, produce
    a unified diff of the changes with ``context`` lines of context. Optionally,
    assign the ``filename`` to the change, and if it is not available, assume
    that the change was performed on stdin/stdout. If no change is detected,
    return an empty string instead of returning an empty unified diff. This is
    comparable to revision control software which only shows differences for
    files that have changed.
    """

    if oldcode == newcode:
        return ""

    if filename:
        difflines = difflib.unified_diff(
            oldcode.split("\n"),
            newcode.split("\n"),
            fromfile=filename,
            tofile=filename,
            lineterm="",
            n=context,
        )
    else:
        difflines = difflib.unified_diff(
            oldcode.split("\n"), newcode.split("\n"), lineterm="", n=context
        )
    return "\n".join(difflines)


def exec_transform_with_prettyprint(
    transform: Codemod,
    code: str,
    *,
    include_generated: bool = False,
    generated_code_marker: str = _DEFAULT_GENERATED_CODE_MARKER,
    format_code: bool = False,
    formatter_args: Sequence[str] = (),
    python_version: Optional[str] = None,
) -> Optional[str]:
    """
    Given an instantiated codemod and a string representing a module, transform that
    code by executing the transform, optionally invoking the formatter and finally
    printing any generated warnings to stderr. If the code includes the generated
    marker at any spot and ``include_generated`` is not set to ``True``, the code
    will not be modified. If ``format_code`` is set to ``False`` or the instantiated
    codemod does not modify the code, the code will not be formatted.  If a
    ``python_version`` is provided, then we will parse the module using
    this version. Otherwise, we will use the version of the currently executing python
    binary.

    In all cases a module will be returned. Whether it is changed depends on the
    input parameters as well as the codemod itself.
    """

    if not include_generated and generated_code_marker in code:
        print(
            "WARNING: Code is generated and we are set to ignore generated code, "
            + "skipping!",
            file=sys.stderr,
        )
        return code

    result = transform_module(transform, code, python_version=python_version)
    maybe_code: Optional[str] = (
        None
        if isinstance(result, (TransformFailure, TransformExit, TransformSkip))
        else result.code
    )

    if maybe_code is not None and format_code:
        try:
            maybe_code = invoke_formatter(formatter_args, maybe_code)
        except Exception as ex:
            # Failed to format code, treat as a failure and make sure that
            # we print the exception for debugging.
            maybe_code = None
            result = TransformFailure(
                error=ex,
                traceback_str=traceback.format_exc(),
                warning_messages=result.warning_messages,
            )

    # Finally, print the output, regardless of what happened
    print_execution_result(result)
    return maybe_code


@dataclass(frozen=True)
class ExecutionResult:
    # File we have results for
    filename: str
    # Whether we actually changed the code for the file or not
    changed: bool
    # The actual result
    transform_result: TransformResult


@dataclass(frozen=True)
class ExecutionConfig:
    blacklist_patterns: Sequence[str] = ()
    format_code: bool = False
    formatter_args: Sequence[str] = ()
    generated_code_marker: str = _DEFAULT_GENERATED_CODE_MARKER
    include_generated: bool = False
    python_version: Optional[str] = None
    repo_root: Optional[str] = None
    unified_diff: Optional[int] = None


def _prepare_context(
    repo_root: str,
    filename: str,
    scratch: Dict[str, object],
    repo_manager: Optional[FullRepoManager],
) -> CodemodContext:
    # determine the module and package name for this file
    try:
        module_name_and_package = calculate_module_and_package(repo_root, filename)
        mod_name = module_name_and_package.name
        pkg_name = module_name_and_package.package
    except ValueError as ex:
        print(f"Failed to determine module name for {filename}: {ex}", file=sys.stderr)
        mod_name = None
        pkg_name = None
    return CodemodContext(
        scratch=scratch,
        filename=filename,
        full_module_name=mod_name,
        full_package_name=pkg_name,
        metadata_manager=repo_manager,
    )


def _instantiate_transformer(
    transformer: Union[Codemod, Type[Codemod]],
    repo_root: str,
    filename: str,
    original_scratch: Dict[str, object],
    codemod_kwargs: Dict[str, object],
    repo_manager: Optional[FullRepoManager],
) -> Codemod:
    if isinstance(transformer, type):
        return transformer(  # type: ignore
            context=_prepare_context(repo_root, filename, {}, repo_manager),
            **codemod_kwargs,
        )
    transformer.context = _prepare_context(
        repo_root, filename, deepcopy(original_scratch), repo_manager
    )
    return transformer


def _check_for_skip(
    filename: str, config: ExecutionConfig
) -> Union[ExecutionResult, bytes]:
    for pattern in config.blacklist_patterns:
        if re.fullmatch(pattern, filename):
            return ExecutionResult(
                filename=filename,
                changed=False,
                transform_result=TransformSkip(
                    skip_reason=SkipReason.BLACKLISTED,
                    skip_description=f"Blacklisted by pattern {pattern}.",
                ),
            )

    with open(filename, "rb") as fp:
        oldcode = fp.read()

    # Skip generated files
    if (
        not config.include_generated
        and config.generated_code_marker.encode("utf-8") in oldcode
    ):
        return ExecutionResult(
            filename=filename,
            changed=False,
            transform_result=TransformSkip(
                skip_reason=SkipReason.GENERATED,
                skip_description="Generated file.",
            ),
        )
    return oldcode


def _execute_transform(
    transformer: Union[Codemod, Type[Codemod]],
    filename: str,
    config: ExecutionConfig,
    original_scratch: Dict[str, object],
    codemod_args: Optional[Dict[str, object]],
    repo_manager: Optional[FullRepoManager],
) -> ExecutionResult:
    warnings: list[str] = []
    try:
        oldcode = _check_for_skip(filename, config)
        if isinstance(oldcode, ExecutionResult):
            return oldcode

        transformer_instance = _instantiate_transformer(
            transformer,
            config.repo_root or ".",
            filename,
            original_scratch,
            codemod_args or {},
            repo_manager,
        )

        # Run the transform, bail if we failed or if we aren't formatting code
        try:
            input_tree = parse_module(
                oldcode,
                config=(
                    PartialParserConfig(python_version=str(config.python_version))
                    if config.python_version is not None
                    else PartialParserConfig()
                ),
            )
            output_tree = transformer_instance.transform_module(input_tree)
            newcode = output_tree.bytes
            encoding = output_tree.encoding
            warnings.extend(transformer_instance.context.warnings)
        except SkipFile as ex:
            warnings.extend(transformer_instance.context.warnings)
            return ExecutionResult(
                filename=filename,
                changed=False,
                transform_result=TransformSkip(
                    skip_reason=SkipReason.OTHER,
                    skip_description=str(ex),
                    warning_messages=warnings,
                ),
            )

        # Call formatter if needed, but only if we actually changed something in this
        # file
        if config.format_code and newcode != oldcode:
            newcode = invoke_formatter(config.formatter_args, newcode)

        # Format as unified diff if needed, otherwise save it back
        changed = oldcode != newcode
        if config.unified_diff:
            newcode = diff_code(
                oldcode.decode(encoding),
                newcode.decode(encoding),
                config.unified_diff,
                filename=filename,
            )
        else:
            # Write back if we changed
            if changed:
                with open(filename, "wb") as fp:
                    fp.write(newcode)
            # Not strictly necessary, but saves space in pickle since we won't use it
            newcode = ""

        # Inform success
        return ExecutionResult(
            filename=filename,
            changed=changed,
            transform_result=TransformSuccess(warning_messages=warnings, code=newcode),
        )

    except KeyboardInterrupt:
        return ExecutionResult(
            filename=filename,
            changed=False,
            transform_result=TransformExit(warning_messages=warnings),
        )
    except Exception as ex:
        return ExecutionResult(
            filename=filename,
            changed=False,
            transform_result=TransformFailure(
                error=ex,
                traceback_str=traceback.format_exc(),
                warning_messages=warnings,
            ),
        )


class Progress:
    ERASE_CURRENT_LINE: str = "\r\033[2K"

    def __init__(self, *, enabled: bool, total: int) -> None:
        self.enabled = enabled
        self.total = total
        # 1/100 = 0, len("0") = 1, precision = 0, more digits for more files
        self.pretty_precision: int = len(str(self.total // 100)) - 1
        # Pretend we start processing immediately. This is not true, but it's
        # close enough to true.
        self.started_at: float = time.time()

    def print(self, finished: int) -> None:
        if not self.enabled:
            return
        left = self.total - finished
        percent = 100.0 * (float(finished) / float(self.total))
        elapsed_time = max(time.time() - self.started_at, 0)

        print(
            f"{self.ERASE_CURRENT_LINE}{self._human_seconds(elapsed_time)} {percent:.{self.pretty_precision}f}% complete, {self.estimate_completion(elapsed_time, finished, left)} estimated for {left} files to go...",
            end="",
            file=sys.stderr,
        )

    def _human_seconds(self, seconds: Union[int, float]) -> str:
        """
        This returns a string which is a human-ish readable elapsed time such
        as 30.42s or 10m 31s
        """

        minutes, seconds = divmod(seconds, 60)
        hours, minutes = divmod(minutes, 60)
        if hours > 0:
            return f"{hours:.0f}h {minutes:02.0f}m {seconds:02.0f}s"
        elif minutes > 0:
            return f"{minutes:02.0f}m {seconds:02.0f}s"
        else:
            return f"{seconds:02.2f}s"

    def estimate_completion(
        self, elapsed_seconds: float, files_finished: int, files_left: int
    ) -> str:
        """
        Computes a really basic estimated completion given a number of
        operations still to do.
        """

        if files_finished <= 0 or elapsed_seconds == 0:
            # Technically infinite but calculating sounds better.
            return "[calculating]"

        fps = files_finished / elapsed_seconds
        estimated_seconds_left = files_left / fps
        return self._human_seconds(estimated_seconds_left)

    def clear(self) -> None:
        if not self.enabled:
            return
        print(self.ERASE_CURRENT_LINE, end="", file=sys.stderr)


def _print_parallel_result(
    exec_result: ExecutionResult,
    progress: Progress,
    *,
    unified_diff: bool,
    show_successes: bool,
    hide_generated: bool,
    hide_blacklisted: bool,
) -> None:
    filename = exec_result.filename
    result = exec_result.transform_result

    if isinstance(result, TransformSkip):
        # Skipped file, print message and don't write back since not changed.
        if not (
            (result.skip_reason is SkipReason.BLACKLISTED and hide_blacklisted)
            or (result.skip_reason is SkipReason.GENERATED and hide_generated)
        ):
            progress.clear()
            print(f"Codemodding {filename}", file=sys.stderr)
            print_execution_result(result)
            print(
                f"Skipped codemodding {filename}: {result.skip_description}\n",
                file=sys.stderr,
            )
    elif isinstance(result, TransformFailure):
        # Print any exception, don't write the file back.
        progress.clear()
        print(f"Codemodding {filename}", file=sys.stderr)
        print_execution_result(result)
        print(f"Failed to codemod {filename}\n", file=sys.stderr)
    elif isinstance(result, TransformSuccess):
        if show_successes or result.warning_messages:
            # Print any warnings, save the changes if there were any.
            progress.clear()
            print(f"Codemodding {filename}", file=sys.stderr)
            print_execution_result(result)
            print(
                f"Successfully codemodded {filename}"
                + (" with warnings\n" if result.warning_messages else "\n"),
                file=sys.stderr,
            )

        # In unified diff mode, the code is a diff we must print.
        if unified_diff and result.code:
            print(result.code)


@dataclass(frozen=True)
class ParallelTransformResult:
    """
    The result of running
    :func:`~libcst.codemod.parallel_exec_transform_with_prettyprint` against
    a series of files. This is a simple summary, with counts for number of
    successfully codemodded files, number of files that we failed to codemod,
    number of warnings generated when running the codemod across the files, and
    the number of files that we skipped when running the codemod.
    """

    #: Number of files that we successfully transformed.
    successes: int
    #: Number of files that we failed to transform.
    failures: int
    #: Number of warnings generated when running transform across files.
    warnings: int
    #: Number of files skipped because they were blacklisted, generated
    #: or the codemod requested to skip.
    skips: int


def parallel_exec_transform_with_prettyprint(  # noqa: C901
    transform: Union[Codemod, Type[Codemod]],
    files: Sequence[str],
    *,
    jobs: Optional[int] = None,
    unified_diff: Optional[int] = None,
    include_generated: bool = False,
    generated_code_marker: str = _DEFAULT_GENERATED_CODE_MARKER,
    format_code: bool = False,
    formatter_args: Sequence[str] = (),
    show_successes: bool = False,
    hide_generated: bool = False,
    hide_blacklisted: bool = False,
    hide_progress: bool = False,
    blacklist_patterns: Sequence[str] = (),
    python_version: Optional[str] = None,
    repo_root: Optional[str] = None,
    codemod_args: Optional[Dict[str, object]] = None,
) -> ParallelTransformResult:
    """
    Given a list of files and a codemod we should apply to them, fork and apply the
    codemod in parallel to all of the files, including any configured formatter. The
    ``jobs`` parameter controls the maximum number of in-flight transforms, and needs to
    be at least 1. If not included, the number of jobs will automatically be set to the
    number of CPU cores. If ``unified_diff`` is set to a number, changes to files will
    be printed to stdout with ``unified_diff`` lines of context. If it is set to
    ``None`` or left out, files themselves will be updated with changes and formatting.
    If a ``python_version`` is provided, then we will parse each source file using this
    version. Otherwise, we will use the version of the currently executing python
    binary.

    A progress indicator as well as any generated warnings will be printed to stderr. To
    supress the interactive progress indicator, set ``hide_progress`` to ``True``. Files
    that include the generated code marker will be skipped unless the
    ``include_generated`` parameter is set to ``True``. Similarly, files that match a
    supplied blacklist of regex patterns will be skipped. Warnings for skipping both
    blacklisted and generated files will be printed to stderr along with warnings
    generated by the codemod unless ``hide_blacklisted`` and ``hide_generated`` are set
    to ``True``. Files that were successfully codemodded will not be printed to stderr
    unless ``show_successes`` is set to ``True``.

    We take a :class:`~libcst.codemod._codemod.Codemod` class, or an instantiated
    :class:`~libcst.codemod._codemod.Codemod`. In the former case, the codemod will be
    instantiated for each file, with ``codemod_args`` passed in to the constructor.
    Passing an already instantiated :class:`~libcst.codemod._codemod.Codemod` is
    deprecated, because it leads to sharing of the
    :class:`~libcst.codemod._codemod.Codemod` instance across files, which is a common
    source of hard-to-track-down bugs when the :class:`~libcst.codemod._codemod.Codemod`
    tracks its state on the instance.
    """

    if isinstance(transform, Codemod):
        warn(
            "Passing transformer instances to `parallel_exec_transform_with_prettyprint` "
            "is deprecated and will break in a future version. "
            "Please pass the transformer class instead.",
            DeprecationWarning,
            stacklevel=2,
        )

    # Ensure that we have no duplicates, otherwise we might get race conditions
    # on write.
    files = sorted({os.path.abspath(f) for f in files})
    total = len(files)
    progress = Progress(enabled=not hide_progress, total=total)

    chunksize = 4
    # Grab number of cores if we need to
    jobs = min(
        jobs if jobs is not None else cpu_count(),
        (len(files) + chunksize - 1) // chunksize,
    )

    if jobs < 1:
        raise ValueError("Must have at least one job to process!")

    if total == 0:
        return ParallelTransformResult(successes=0, failures=0, skips=0, warnings=0)

    metadata_manager: Optional[FullRepoManager] = None
    if repo_root is not None:
        # Make sure if there is a root that we have the absolute path to it.
        repo_root = os.path.abspath(repo_root)
        # Spin up a full repo metadata manager so that we can provide metadata
        # like type inference to individual forked processes.
        print("Calculating full-repo metadata...", file=sys.stderr)
        metadata_manager = FullRepoManager(
            repo_root,
            files,
            transform.get_inherited_dependencies(),
        )
        metadata_manager.resolve_cache()

    print("Executing codemod...", file=sys.stderr)

    config = ExecutionConfig(
        repo_root=repo_root,
        unified_diff=unified_diff,
        include_generated=include_generated,
        generated_code_marker=generated_code_marker,
        format_code=format_code,
        formatter_args=formatter_args,
        blacklist_patterns=blacklist_patterns,
        python_version=python_version,
    )

    pool_impl: Callable[[], Executor]
    if total == 1 or jobs == 1:
        # Simple case, we should not pay for process overhead.
        # Let's just use a dummy synchronous executor.
        jobs = 1
        pool_impl = DummyExecutor
    elif getattr(sys, "_is_gil_enabled", lambda: True)():  # pyre-ignore[16]
        from concurrent.futures import ProcessPoolExecutor

        pool_impl = functools.partial(ProcessPoolExecutor, max_workers=jobs)
        # Warm the parser, pre-fork.
        parse_module(
            "",
            config=(
                PartialParserConfig(python_version=python_version)
                if python_version is not None
                else PartialParserConfig()
            ),
        )
    else:
        from concurrent.futures import ThreadPoolExecutor

        pool_impl = functools.partial(ThreadPoolExecutor, max_workers=jobs)

    successes: int = 0
    failures: int = 0
    warnings: int = 0
    skips: int = 0
    original_scratch = (
        deepcopy(transform.context.scratch) if isinstance(transform, Codemod) else {}
    )

    with pool_impl() as executor:  # type: ignore
        try:
            futures = [
                executor.submit(
                    _execute_transform,
                    transformer=transform,
                    filename=filename,
                    config=config,
                    original_scratch=original_scratch,
                    codemod_args=codemod_args,
                    repo_manager=metadata_manager,
                )
                for filename in files
            ]
            for future in as_completed(futures):
                result = future.result()
                # Print an execution result, keep track of failures
                _print_parallel_result(
                    result,
                    progress,
                    unified_diff=bool(unified_diff),
                    show_successes=show_successes,
                    hide_generated=hide_generated,
                    hide_blacklisted=hide_blacklisted,
                )
                progress.print(successes + failures + skips)

                if isinstance(result.transform_result, TransformFailure):
                    failures += 1
                elif isinstance(result.transform_result, TransformSuccess):
                    successes += 1
                elif isinstance(
                    result.transform_result, (TransformExit, TransformSkip)
                ):
                    skips += 1

                warnings += len(result.transform_result.warning_messages)
        finally:
            progress.clear()

    # Return whether there was one or more failure.
    return ParallelTransformResult(
        successes=successes, failures=failures, skips=skips, warnings=warnings
    )
