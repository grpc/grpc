# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

"""
Provides everything needed to run a CodemodCommand.
"""

import traceback
from dataclasses import dataclass
from enum import Enum
from typing import Optional, Sequence, Union

from libcst import parse_module, PartialParserConfig
from libcst.codemod._codemod import Codemod

# All datastructures defined in this class are pickleable so that they can be used
# as a return value with the multiprocessing module.


@dataclass(frozen=True)
class TransformSuccess:
    """
    A :class:`~libcst.codemod.TransformResult` used when the codemod was successful.
    Stores all the information we might need to display to the user upon success, as
    well as the transformed file contents.
    """

    #: All warning messages that were generated during the codemod.
    warning_messages: Sequence[str]

    #: The updated code, post-codemod.
    code: str


@dataclass(frozen=True)
class TransformFailure:
    """
    A :class:`~libcst.codemod.TransformResult` used when the codemod failed.
    Stores all the information we might need to display to the user upon a failure.
    """

    #: All warning messages that were generated before the codemod crashed.
    warning_messages: Sequence[str]

    #: The exception that was raised during the codemod.
    error: Exception

    #: The traceback string that was recorded at the time of exception.
    traceback_str: str


@dataclass(frozen=True)
class TransformExit:
    """
    A :class:`~libcst.codemod.TransformResult` used when the codemod was interrupted
    by the user (e.g. KeyboardInterrupt).
    """

    #: An empty list of warnings, included so that all
    #: :class:`~libcst.codemod.TransformResult` have a ``warning_messages`` attribute.
    warning_messages: Sequence[str] = ()


class SkipReason(Enum):
    """
    An enumeration of all valid reasons for a codemod to skip.
    """

    #: The module was skipped because we detected that it was generated code, and
    #: we were configured to skip generated files.
    GENERATED = "generated"

    #: The module was skipped because we detected that it was blacklisted, and we
    #: were configured to skip blacklisted files.
    BLACKLISTED = "blacklisted"

    #: The module was skipped because the codemod requested us to skip using the
    #: :class:`~libcst.codemod.SkipFile` exception.
    OTHER = "other"


@dataclass(frozen=True)
class TransformSkip:
    """
    A :class:`~libcst.codemod.TransformResult` used when the codemod requested to
    be skipped. This could be because it's a generated file, or due to filename
    blacklist, or because the transform raised :class:`~libcst.codemod.SkipFile`.
    """

    #: The reason that we skipped codemodding this module.
    skip_reason: SkipReason

    #: The description populated from the :class:`~libcst.codemod.SkipFile` exception.
    skip_description: str

    #: All warning messages that were generated before the codemod decided to skip.
    warning_messages: Sequence[str] = ()


class SkipFile(Exception):
    """
    Raise this exception to skip codemodding the current file.

    The exception message should be the reason for skipping.
    """


TransformResult = Union[
    TransformSuccess, TransformFailure, TransformExit, TransformSkip
]


def transform_module(
    transformer: Codemod, code: str, *, python_version: Optional[str] = None
) -> TransformResult:
    """
    Given a module as represented by a string and a :class:`~libcst.codemod.Codemod`
    that we wish to run, execute the codemod on the code and return a
    :class:`~libcst.codemod.TransformResult`. This should never raise an exception.
    On success, this returns a :class:`~libcst.codemod.TransformSuccess` containing
    any generated warnings as well as the transformed code. If the codemod is
    interrupted with a Ctrl+C, this returns a :class:`~libcst.codemod.TransformExit`.
    If the codemod elected to skip by throwing a :class:`~libcst.codemod.SkipFile`
    exception, this will return a :class:`~libcst.codemod.TransformSkip` containing
    the reason for skipping as well as any warnings that were generated before
    the codemod decided to skip. If the codemod throws an unexpected exception,
    this will return a :class:`~libcst.codemod.TransformFailure` containing the
    exception that occured as well as any warnings that were generated before the
    codemod crashed.
    """
    try:
        input_tree = parse_module(
            code,
            config=(
                PartialParserConfig(python_version=python_version)
                if python_version is not None
                else PartialParserConfig()
            ),
        )
        output_tree = transformer.transform_module(input_tree)
        return TransformSuccess(
            code=output_tree.code, warning_messages=transformer.context.warnings
        )
    except KeyboardInterrupt:
        return TransformExit()
    except SkipFile as ex:
        return TransformSkip(
            skip_description=str(ex),
            skip_reason=SkipReason.OTHER,
            warning_messages=transformer.context.warnings,
        )
    except Exception as ex:
        return TransformFailure(
            error=ex,
            traceback_str=traceback.format_exc(),
            warning_messages=transformer.context.warnings,
        )
