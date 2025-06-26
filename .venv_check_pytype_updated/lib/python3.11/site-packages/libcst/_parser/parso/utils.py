# Copyright 2004-2005 Elemental Security, Inc. All Rights Reserved.
# Licensed to PSF under a Contributor Agreement.
#
# Modifications:
# Copyright David Halter and Contributors
# Modifications are dual-licensed: MIT and PSF.
# 99% of the code is different from pgen2, now.
#
# A fork of `parso.utils`.
# https://github.com/davidhalter/parso/blob/master/parso/utils.py
#
# The following changes were made:
# - Drop Python 2 compatibility layer
# - Use dataclasses instead of namedtuple
# - Apply type hints directly to files
# - Make PythonVersionInfo directly usable in hashmaps
# - Unroll total ordering because Pyre doesn't understand it


import re
import sys
from ast import literal_eval
from dataclasses import dataclass
from typing import Optional, Sequence, Tuple, Union

# The following is a list in Python that are line breaks in str.splitlines, but
# not in Python. In Python only \r (Carriage Return, 0xD) and \n (Line Feed,
# 0xA) are allowed to split lines.
_NON_LINE_BREAKS = (
    "\v",  # Vertical Tabulation 0xB
    "\f",  # Form Feed 0xC
    "\x1c",  # File Separator
    "\x1d",  # Group Separator
    "\x1e",  # Record Separator
    "\x85",  # Next Line (NEL - Equivalent to CR+LF.
    # Used to mark end-of-line on some IBM mainframes.)
    "\u2028",  # Line Separator
    "\u2029",  # Paragraph Separator
)


@dataclass(frozen=True)
class Version:
    major: int
    minor: int
    micro: int


def split_lines(string: str, keepends: bool = False) -> Sequence[str]:
    r"""
    Intended for Python code. In contrast to Python's :py:meth:`str.splitlines`,
    looks at form feeds and other special characters as normal text. Just
    splits ``\n`` and ``\r\n``.
    Also different: Returns ``[""]`` for an empty string input.

    In Python 2.7 form feeds are used as normal characters when using
    str.splitlines. However in Python 3 somewhere there was a decision to split
    also on form feeds.
    """
    if keepends:
        lst = string.splitlines(True)

        # We have to merge lines that were broken by form feed characters.
        merge = []
        for i, line in enumerate(lst):
            try:
                last_chr = line[-1]
            except IndexError:
                pass
            else:
                if last_chr in _NON_LINE_BREAKS:
                    merge.append(i)

        for index in reversed(merge):
            try:
                lst[index] = lst[index] + lst[index + 1]
                del lst[index + 1]
            except IndexError:
                # index + 1 can be empty and therefore there's no need to
                # merge.
                pass

        # The stdlib's implementation of the end is inconsistent when calling
        # it with/without keepends. One time there's an empty string in the
        # end, one time there's none.
        if string.endswith("\n") or string.endswith("\r") or string == "":
            lst.append("")
        return lst
    else:
        return re.split(r"\n|\r\n|\r", string)


def python_bytes_to_unicode(
    source: Union[str, bytes], encoding: str = "utf-8", errors: str = "strict"
) -> str:
    """
    Checks for unicode BOMs and PEP 263 encoding declarations. Then returns a
    unicode object like in :py:meth:`bytes.decode`.

    :param encoding: See :py:meth:`bytes.decode` documentation.
    :param errors: See :py:meth:`bytes.decode` documentation. ``errors`` can be
        ``'strict'``, ``'replace'`` or ``'ignore'``.
    """

    def detect_encoding() -> Union[str, bytes]:
        """
        For the implementation of encoding definitions in Python, look at:
        - http://www.python.org/dev/peps/pep-0263/
        - http://docs.python.org/2/reference/lexical_analysis.html#encoding-declarations
        """
        byte_mark = literal_eval(r"b'\xef\xbb\xbf'")
        if source.startswith(byte_mark):
            # UTF-8 byte-order mark
            return b"utf-8"

        # pyre-ignore Pyre can't see that Union[str, bytes] conforms to AnyStr.
        first_two_match = re.match(rb"(?:[^\n]*\n){0,2}", source)
        if first_two_match is None:
            return encoding
        first_two_lines = first_two_match.group(0)
        possible_encoding = re.search(rb"coding[=:]\s*([-\w.]+)", first_two_lines)
        if possible_encoding:
            return possible_encoding.group(1)
        else:
            # the default if nothing else has been set -> PEP 263
            return encoding

    if isinstance(source, str):
        # only cast bytes
        return source

    actual_encoding = detect_encoding()
    if not isinstance(actual_encoding, str):
        actual_encoding = actual_encoding.decode("utf-8", "replace")

    # Cast to str
    return source.decode(actual_encoding, errors)


@dataclass(frozen=True)
class PythonVersionInfo:
    major: int
    minor: int

    def __gt__(self, other: Union["PythonVersionInfo", Tuple[int, int]]) -> bool:
        if isinstance(other, tuple):
            if len(other) != 2:
                raise ValueError("Can only compare to tuples of length 2.")
            return (self.major, self.minor) > other

        return (self.major, self.minor) > (other.major, other.minor)

    def __ge__(self, other: Union["PythonVersionInfo", Tuple[int, int]]) -> bool:
        return self.__gt__(other) or self.__eq__(other)

    def __lt__(self, other: Union["PythonVersionInfo", Tuple[int, int]]) -> bool:
        if isinstance(other, tuple):
            if len(other) != 2:
                raise ValueError("Can only compare to tuples of length 2.")
            return (self.major, self.minor) < other

        return (self.major, self.minor) < (other.major, other.minor)

    def __le__(self, other: Union["PythonVersionInfo", Tuple[int, int]]) -> bool:
        return self.__lt__(other) or self.__eq__(other)

    def __eq__(self, other: Union["PythonVersionInfo", Tuple[int, int]]) -> bool:
        if isinstance(other, tuple):
            if len(other) != 2:
                raise ValueError("Can only compare to tuples of length 2.")
            return (self.major, self.minor) == other

        return (self.major, self.minor) == (other.major, other.minor)

    def __ne__(self, other: Union["PythonVersionInfo", Tuple[int, int]]) -> bool:
        return not self.__eq__(other)

    def __hash__(self) -> int:
        return hash((self.major, self.minor))


def _parse_version(version: str) -> PythonVersionInfo:
    match = re.match(r"(\d+)(?:\.(\d+)(?:\.\d+)?)?$", version)
    if match is None:
        raise ValueError(
            (
                "The given version is not in the right format. "
                + 'Use something like "3.2" or "3".'
            )
        )

    major = int(match.group(1))
    minor = match.group(2)
    if minor is None:
        # Use the latest Python in case it's not exactly defined, because the
        # grammars are typically backwards compatible?
        if major == 2:
            minor = "7"
        elif major == 3:
            minor = "6"
        else:
            raise NotImplementedError(
                "Sorry, no support yet for those fancy new/old versions."
            )
    minor = int(minor)
    return PythonVersionInfo(major, minor)


def parse_version_string(version: Optional[str] = None) -> PythonVersionInfo:
    """
    Checks for a valid version number (e.g. `3.2` or `2.7.1` or `3`) and
    returns a corresponding version info that is always two characters long in
    decimal.
    """
    if version is None:
        version = "%s.%s" % sys.version_info[:2]

    return _parse_version(version)
