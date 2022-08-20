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
import logging
from typing import Any, Callable, List, Optional, Sequence

import tenacity
from tenacity import _utils as tenacity_utils
from tenacity import stop
from tenacity import wait
from tenacity.retry import retry_base

retryers_logger = logging.getLogger(__name__)
# Type aliases
timedelta = datetime.timedelta
Retrying = tenacity.Retrying
CheckResultFn = Callable[[Any], bool]


def _retry_on_exceptions(retry_on_exceptions: Optional[Sequence[Any]] = None):
    # Retry on all exceptions by default
    if retry_on_exceptions is None:
        retry_on_exceptions = (Exception,)
    return tenacity.retry_if_exception_type(retry_on_exceptions)


def _build_retry_conditions(
        *,
        retry_on_exceptions: Optional[Sequence[Any]] = None,
        check_result: Optional[CheckResultFn] = None) -> List[retry_base]:
    retry_conditions = [_retry_on_exceptions(retry_on_exceptions)]
    if check_result is not None:
        retry_conditions.append(tenacity.retry_if_not_result(check_result))
    return retry_conditions


def exponential_retryer_with_timeout(
        *,
        wait_min: timedelta,
        wait_max: timedelta,
        timeout: timedelta,
        retry_on_exceptions: Optional[Sequence[Any]] = None,
        check_result: Optional[CheckResultFn] = None,
        logger: Optional[logging.Logger] = None,
        log_level: Optional[int] = logging.DEBUG) -> Retrying:
    if logger is None:
        logger = retryers_logger
    if log_level is None:
        log_level = logging.DEBUG

    retry_conditions = _build_retry_conditions(
        retry_on_exceptions=retry_on_exceptions, check_result=check_result)
    retry_error_callback = _on_error_callback(timeout=timeout,
                                              check_result=check_result)
    return Retrying(retry=tenacity.retry_any(*retry_conditions),
                    wait=wait.wait_exponential(min=wait_min.total_seconds(),
                                               max=wait_max.total_seconds()),
                    stop=stop.stop_after_delay(timeout.total_seconds()),
                    before_sleep=tenacity.before_sleep_log(logger, log_level),
                    retry_error_callback=retry_error_callback)


def constant_retryer(*,
                     wait_fixed: timedelta,
                     attempts: int = 0,
                     timeout: Optional[timedelta] = None,
                     retry_on_exceptions: Optional[Sequence[Any]] = None,
                     check_result: Optional[CheckResultFn] = None,
                     logger: Optional[logging.Logger] = None,
                     log_level: Optional[int] = logging.DEBUG) -> Retrying:
    if logger is None:
        logger = retryers_logger
    if log_level is None:
        log_level = logging.DEBUG
    if attempts < 1 and timeout is None:
        raise ValueError('The number of attempts or the timeout must be set')
    stops = []
    if attempts > 0:
        stops.append(stop.stop_after_attempt(attempts))
    if timeout is not None:
        stops.append(stop.stop_after_delay(timeout.total_seconds()))

    retry_conditions = _build_retry_conditions(
        retry_on_exceptions=retry_on_exceptions, check_result=check_result)
    retry_error_callback = _on_error_callback(timeout=timeout,
                                              attempts=attempts,
                                              check_result=check_result)
    return Retrying(retry=tenacity.retry_any(*retry_conditions),
                    wait=wait.wait_fixed(wait_fixed.total_seconds()),
                    stop=stop.stop_any(*stops),
                    before_sleep=tenacity.before_sleep_log(logger, log_level),
                    retry_error_callback=retry_error_callback)


def _on_error_callback(*,
                       timeout: Optional[timedelta] = None,
                       attempts: int = 0,
                       check_result: Optional[CheckResultFn] = None):

    def error_handler(retry_state: tenacity.RetryCallState):
        raise RetryError(retry_state,
                         timeout=timeout,
                         attempts=attempts,
                         check_result=check_result)

    return error_handler


class RetryError(tenacity.RetryError):

    def __init__(self,
                 retry_state,
                 *,
                 timeout: Optional[timedelta] = None,
                 attempts: int = 0,
                 check_result: Optional[CheckResultFn] = None):
        super().__init__(retry_state.outcome)
        callback_name = tenacity_utils.get_callback_name(retry_state.fn)
        self.message = f'Retry error calling {callback_name}:'
        if timeout:
            self.message += f' timeout {timeout} exceeded'
            if attempts:
                self.message += ' or'
        if attempts:
            self.message += f' {attempts} attempts exhausted'

        self.message += '.'

        if retry_state.outcome.failed:
            ex = retry_state.outcome.exception()
            self.message += f' Last exception: {type(ex).__name__}: {ex}'
        elif check_result:
            self.message += ' Check result callback returned False.'

    def result(self, *, default=None):
        return default if self.last_attempt.failed else self.last_attempt.result(
        )

    def __str__(self):
        return self.message
