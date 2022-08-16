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
from typing import Dict, Optional
import urllib.parse


class RunnerError(Exception):
    """Error running xDS Test App running remotely."""


class BaseRunner(metaclass=ABCMeta):

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
            request: Optional[Dict[str, str]] = None) -> str:
        req_merged = {'query': cls._logs_explorer_query(query)}
        if request is not None:
            req_merged.update(request)

        req = cls._logs_explorer_request(req_merged)
        return f'https://{gcp_ui_url}/logs/query;{req}?project={gcp_project}'

    @classmethod
    def _logs_explorer_query(cls, query: Dict[str, str]) -> str:
        return '\n'.join(f'{k}="{v}"' for k, v in query.items())

    @classmethod
    def _logs_explorer_request(cls, req: Dict[str, str]) -> str:
        return ';'.join(
            f'{k}={cls._logs_explorer_quote(v)}' for k, v in req.items())

    @classmethod
    def _logs_explorer_quote(cls, value: str) -> str:
        return urllib.parse.quote_plus(value, safe=':')
