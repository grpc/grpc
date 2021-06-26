# Copyright 2021 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""This contains common helpers for working with dates and time."""
import datetime
import re
from typing import Pattern

RE_ZERO_OFFSET: Pattern[str] = re.compile(r'[+\-]00:?00$')


def utc_now() -> datetime.datetime:
    """Construct a datetime from current time in UTC timezone."""
    return datetime.datetime.now(datetime.timezone.utc)


def shorten_utc_zone(utc_datetime_str: str) -> str:
    """Replace ±00:00 timezone designator with Z (zero offset AKA Zulu time)."""
    return RE_ZERO_OFFSET.sub('Z', utc_datetime_str)


def iso8601_basic_time() -> str:
    """Return current UTC datetime in ISO-8601 format with "basic" time format.

    Basic time format is is T[hh][mm][ss] (no colon separator)
    Ref https://en.wikipedia.org/wiki/ISO_8601#Times

    Example: TODO
    Useful in naming resources, where ":" and "+" chars aren't allowed.
    """
    iso8601_extended_time: str = utc_now().isoformat(timespec='seconds')
    return shorten_utc_zone(iso8601_extended_time).replace(':', '')
