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


def datetime_suffix(*, seconds: bool = True) -> str:
    """Return current UTC date, and time in a format useful for resource naming.

    Examples:
        - 2021-06-26-185942 (seconds=True)
        - 2021-06-26-1859   (seconds=False)
    Use in resources names incompatible with ISO 8601, e.g. some GCP resources
    that only allow lowercase alphanumeric chars and dashes.

    Hours and minutes are joined together for better readability, so time is
    visually distinct from dash-separated date.
    """
    return utc_now().strftime('%Y-%m-%d-%H%M' + ('%S' if seconds else ''))
