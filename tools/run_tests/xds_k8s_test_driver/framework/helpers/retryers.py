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
from typing import Any, Callable, List, Optional, Tuple, Type

import tenacity
from tenacity import _utils as tenacity_utils
from tenacity import compat as tenacity_compat
from tenacity import stop
from tenacity import wait
from tenacity.retry import retry_base

retryers_logger = logging.getLogger(__name__)
# Type aliases
timedelta = datetime.timedelta
Retrying = tenacity.Retrying
CheckResultFn = Callable[[Any], bool]
_ExceptionClasses = Tuple[Type[Exception], ...]


def _build_retry_conditions(
    *,
    retry_on_exceptions: Optional[_ExceptionClasses] = None,
    check_result: Optional[CheckResultFn] = None,
) -> List[retry_base]:
    # Retry on all exceptions by default
    if retry_on_exceptions is None:
        retry_on_exceptions = (Exception,)

    retry_conditions = [tenacity.retry_if_exception_type(retry_on_exceptions)]
    if check_result is not None:
        if retry_on_exceptions:
            # When retry_on_exceptions is set, also catch them while executing
            # check_result callback.
            check_result = _safe_check_result(check_result, retry_on_exceptions)
        retry_conditions.append(tenacity.retry_if_not_result(check_result))
    return retry_conditions


def exponential_retryer_with_timeout(
    *,
    wait_min: timedelta,
    wait_max: timedelta,
    timeout: timedelta,
    retry_on_exceptions: Optional[_ExceptionClasses] = None,
    check_result: Optional[CheckResultFn] = None,
    logger: Optional[logging.Logger] = None,
    log_level: Optional[int] = logging.DEBUG,
) -> Retrying:
    if logger is None:
        logger = retryers_logger
    if log_level is None:
        log_level = logging.DEBUG

    retry_conditions = _build_retry_conditions(
        retry_on_exceptions=retry_on_exceptions, check_result=check_result
    )
    retry_error_callback = _on_error_callback(
        timeout=timeout, check_result=check_result
    )
    return Retrying(
        retry=tenacity.retry_any(*retry_conditions),
        wait=wait.wait_exponential(
            min=wait_min.total_seconds(), max=wait_max.total_seconds()
        ),
        stop=stop.stop_after_delay(timeout.total_seconds()),
        before_sleep=_before_sleep_log(logger, log_level),
        retry_error_callback=retry_error_callback,
    )


def constant_retryer(
    *,
    wait_fixed: timedelta,
    attempts: int = 0,
    timeout: Optional[timedelta] = None,
    retry_on_exceptions: Optional[_ExceptionClasses] = None,
    check_result: Optional[CheckResultFn] = None,
    logger: Optional[logging.Logger] = None,
    log_level: Optional[int] = logging.DEBUG,
) -> Retrying:
    if logger is None:
        logger = retryers_logger
    if log_level is None:
        log_level = logging.DEBUG
    if attempts < 1 and timeout is None:
        raise ValueError("The number of attempts or the timeout must be set")
    stops = []
    if attempts > 0:
        stops.append(stop.stop_after_attempt(attempts))
    if timeout is not None:
        stops.append(stop.stop_after_delay(timeout.total_seconds()))

    retry_conditions = _build_retry_conditions(
        retry_on_exceptions=retry_on_exceptions, check_result=check_result
    )
    retry_error_callback = _on_error_callback(
        timeout=timeout, attempts=attempts, check_result=check_result
    )
    return Retrying(
        retry=tenacity.retry_any(*retry_conditions),
        wait=wait.wait_fixed(wait_fixed.total_seconds()),
        stop=stop.stop_any(*stops),
        before_sleep=_before_sleep_log(logger, log_level),
        retry_error_callback=retry_error_callback,
    )


def _on_error_callback(
    *,
    timeout: Optional[timedelta] = None,
    attempts: int = 0,
    check_result: Optional[CheckResultFn] = None,
):
    """A helper to propagate the initial state to the RetryError, so that
    it can assemble a helpful message containing timeout/number of attempts.
    """

    def error_handler(retry_state: tenacity.RetryCallState):
        raise RetryError(
            retry_state,
            timeout=timeout,
            attempts=attempts,
            check_result=check_result,
        )

    return error_handler


def _safe_check_result(
    check_result: CheckResultFn, retry_on_exceptions: _ExceptionClasses
) -> CheckResultFn:
    """Wraps check_result callback to catch and handle retry_on_exceptions.

    Normally tenacity doesn't retry when retry_if_result/retry_if_not_result
    raise an error. This wraps the callback to automatically catch Exceptions
    specified in the retry_on_exceptions argument.

    Ideally we should make all check_result callbacks to not throw, but
    in case it does, we'd rather be annoying in the logs, than break the test.
    """

    def _check_result_wrapped(result):
        try:
            return check_result(result)
        except retry_on_exceptions:
            retryers_logger.warning(
                (
                    "Result check callback %s raised an exception."
                    "This shouldn't happen, please handle any exceptions and "
                    "return return a boolean."
                ),
                tenacity_utils.get_callback_name(check_result),
                exc_info=True,
            )
            return False

    return _check_result_wrapped


def _before_sleep_log(logger, log_level, exc_info=False):
    """Same as tenacity.before_sleep_log, but only logs primitive return values.
    This is not useful when the return value is a dump of a large object.
    """

    def log_it(retry_state):
        if retry_state.outcome.failed:
            ex = retry_state.outcome.exception()
            verb, value = "raised", "%s: %s" % (type(ex).__name__, ex)

            if exc_info:
                local_exc_info = tenacity_compat.get_exc_info_from_future(
                    retry_state.outcome
                )
            else:
                local_exc_info = False
        else:
            local_exc_info = False  # exc_info does not apply when no exception
            result = retry_state.outcome.result()
            if isinstance(result, (int, bool, str)):
                verb, value = "returned", result
            else:
                verb, value = "returned type", type(result)

        logger.log(
            log_level,
            "Retrying %s in %s seconds as it %s %s.",
            tenacity_utils.get_callback_name(retry_state.fn),
            getattr(retry_state.next_action, "sleep"),
            verb,
            value,
            exc_info=local_exc_info,
        )

    return log_it


class RetryError(tenacity.RetryError):
    def __init__(
        self,
        retry_state,
        *,
        timeout: Optional[timedelta] = None,
        attempts: int = 0,
        check_result: Optional[CheckResultFn] = None,
    ):
        super().__init__(retry_state.outcome)
        callback_name = tenacity_utils.get_callback_name(retry_state.fn)
        self.message = f"Retry error calling {callback_name}:"
        if timeout:
            self.message += f" timeout {timeout} (h:mm:ss) exceeded"
            if attempts:
                self.message += " or"
        if attempts:
            self.message += f" {attempts} attempts exhausted"

        self.message += "."

        if retry_state.outcome.failed:
            ex = retry_state.outcome.exception()
            self.message += f" Last exception: {type(ex).__name__}: {ex}"
        elif check_result:
            self.message += " Check result callback returned False."

    def result(self, *, default=None):
        return (
            default if self.last_attempt.failed else self.last_attempt.result()
        )

    def __str__(self):
        return self.message
