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
from typing import Optional, Pattern

import dateutil.parser

RE_ZERO_OFFSET: Pattern[str] = re.compile(r"[+\-]00:?00$")


def utc_now() -> datetime.datetime:
    """Construct a datetime from current time in UTC timezone."""
    return datetime.datetime.now(datetime.timezone.utc)


def shorten_utc_zone(utc_datetime_str: str) -> str:
    """Replace ±00:00 timezone designator with Z (zero offset AKA Zulu time)."""
    return RE_ZERO_OFFSET.sub("Z", utc_datetime_str)


def iso8601_utc_time(time: datetime.datetime = None) -> str:
    """Converts datetime UTC and formats as ISO-8601 Zulu time."""
    utc_time = time.astimezone(tz=datetime.timezone.utc)
    return shorten_utc_zone(utc_time.isoformat())


def iso8601_to_datetime(date_str: str) -> datetime.datetime:
    # TODO(sergiitk): use regular datetime.datetime when upgraded to py3.11.
    return dateutil.parser.isoparse(date_str)


def datetime_suffix(*, seconds: bool = False) -> str:
    """Return current UTC date, and time in a format useful for resource naming.

    Examples:
        - 20210626-1859   (seconds=False)
        - 20210626-185942 (seconds=True)
    Use in resources names incompatible with ISO 8601, e.g. some GCP resources
    that only allow lowercase alphanumeric chars and dashes.

    Hours and minutes are joined together for better readability, so time is
    visually distinct from dash-separated date.
    """
    return utc_now().strftime("%Y%m%d-%H%M" + ("%S" if seconds else ""))


def ago(date_from: datetime.datetime, now: Optional[datetime.datetime] = None):
    if not now:
        now = utc_now()

    # Round down microseconds.
    date_from = date_from.replace(microsecond=0)
    now = now.replace(microsecond=0)

    # Calculate the diff.
    delta: datetime.timedelta = now - date_from

    if delta.days > 1:
        result = f"{delta.days} days"
    elif delta.days > 0:
        result = f"{delta.days} day"
    else:
        # This case covers negative deltas too.
        result = f"{delta} (h:mm:ss)"

    return f"{result} ago"
