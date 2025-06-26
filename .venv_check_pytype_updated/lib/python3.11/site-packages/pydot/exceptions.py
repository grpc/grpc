# SPDX-FileCopyrightText: 2024 pydot contributors
#
# SPDX-License-Identifier: MIT

"""Exception classes for pydot."""

from __future__ import annotations


class PydotException(Exception):
    """Base class for exceptions in Pydot.

    This base class will not be raised directly.

    Catch this base class to catch all derived exceptions, though be
    aware that pydot may raise Python built-in exceptions or pyparsing
    exceptions as well.
    """


class Error(PydotException):
    """General error handling class."""

    def __init__(self, value: str) -> None:
        self.value = value

    def __str__(self) -> str:
        return self.value
