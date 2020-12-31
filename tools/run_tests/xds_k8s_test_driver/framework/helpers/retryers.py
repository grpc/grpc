# Copyright 2020 gRPC authors.
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
"""This contains common retrying helpers (retryers).

We use tenacity as a general-purpose retrying library.

> It [tenacity] originates from a fork of retrying which is sadly no
> longer maintained. Tenacity isn’t api compatible with retrying but >
> adds significant new functionality and fixes a number of longstanding bugs.
> - https://tenacity.readthedocs.io/en/latest/index.html
"""
import datetime
from typing import Any, List, Optional

import tenacity

# Type aliases
timedelta = datetime.timedelta
Retrying = tenacity.Retrying
_retry_if_exception_type = tenacity.retry_if_exception_type
_stop_after_delay = tenacity.stop_after_delay
_wait_exponential = tenacity.wait_exponential


def _retry_on_exceptions(retry_on_exceptions: Optional[List[Any]] = None):
    # Retry on all exceptions by default
    if retry_on_exceptions is None:
        retry_on_exceptions = (Exception,)
    return _retry_if_exception_type(retry_on_exceptions)


def exponential_retryer_with_timeout(
        *,
        wait_min: timedelta,
        wait_max: timedelta,
        timeout: timedelta,
        retry_on_exceptions: Optional[List[Any]] = None) -> Retrying:
    return Retrying(retry=_retry_on_exceptions(retry_on_exceptions),
                    wait=_wait_exponential(min=wait_min.total_seconds(),
                                           max=wait_max.total_seconds()),
                    stop=_stop_after_delay(timeout.total_seconds()),
                    reraise=True)
