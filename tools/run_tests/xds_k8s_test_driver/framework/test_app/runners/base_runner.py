# Copyright 2022 gRPC authors.
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
"""
Common functionality for running xDS Test Client and Server remotely.
"""
from abc import ABCMeta
from abc import abstractmethod
import functools
import pathlib
import threading
from typing import Dict, Optional
import urllib.parse

from absl import flags

from framework import xds_flags
from framework.helpers import logs

flags.adopt_module_key_flags(logs)
_LOGS_SUBDIR = "test_app_logs"


class RunnerError(Exception):
    """Error running xDS Test App running remotely."""


class BaseRunner(metaclass=ABCMeta):
    _logs_subdir: Optional[pathlib.Path] = None
    _log_stop_event: Optional[threading.Event] = None

    def __init__(self):
        if xds_flags.COLLECT_APP_LOGS.value:
            self._logs_subdir = logs.log_dir_mkdir(_LOGS_SUBDIR)
            self._log_stop_event = threading.Event()

    @property
    @functools.lru_cache(None)
    def should_collect_logs(self) -> bool:
        return self._logs_subdir is not None

    @property
    @functools.lru_cache(None)
    def logs_subdir(self) -> pathlib.Path:
        if not self.should_collect_logs:
            raise FileNotFoundError("Log collection is not enabled.")
        return self._logs_subdir

    @property
    def log_stop_event(self) -> threading.Event:
        if not self.should_collect_logs:
            raise ValueError("Log collection is not enabled.")
        return self._log_stop_event

    def maybe_stop_logging(self):
        if self.should_collect_logs and not self.log_stop_event.is_set():
            self.log_stop_event.set()

    @abstractmethod
    def run(self, **kwargs):
        pass

    @abstractmethod
    def cleanup(self, *, force=False):
        pass

    @classmethod
    def _logs_explorer_link_from_params(
        cls,
        *,
        gcp_ui_url: str,
        gcp_project: str,
        query: Dict[str, str],
        request: Optional[Dict[str, str]] = None,
    ) -> str:
        req_merged = {"query": cls._logs_explorer_query(query)}
        if request is not None:
            req_merged.update(request)

        req = cls._logs_explorer_request(req_merged)
        return f"https://{gcp_ui_url}/logs/query;{req}?project={gcp_project}"

    @classmethod
    def _logs_explorer_query(cls, query: Dict[str, str]) -> str:
        return "\n".join(f'{k}="{v}"' for k, v in query.items())

    @classmethod
    def _logs_explorer_request(cls, req: Dict[str, str]) -> str:
        return ";".join(
            f"{k}={cls._logs_explorer_quote(v)}" for k, v in req.items()
        )

    @classmethod
    def _logs_explorer_quote(cls, value: str) -> str:
        return urllib.parse.quote_plus(value, safe=":")
