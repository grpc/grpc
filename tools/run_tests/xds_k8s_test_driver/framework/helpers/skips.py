# Copyright 2022 The gRPC Authors
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
"""The classes and predicates to assist validate test config for test cases."""
from dataclasses import dataclass
import functools
import re
from typing import Callable
import unittest

from packaging import version as pkg_version

from framework import xds_flags
from framework import xds_k8s_flags


def _get_lang(image_name: str) -> str:
    return re.search(r'/(\w+)-(client|server):', image_name).group(1)


def _parse_version(s: str) -> pkg_version.Version:
    if s.endswith(".x"):
        s = s[:-2]
    return pkg_version.Version(s)


@dataclass
class TestConfig:
    """Describes the config for the test suite."""
    client_lang: str
    server_lang: str
    version: str

    # Languages supported by the framework, that follow the same gRPC version
    # release scheme.
    common_langs = frozenset({'cpp', 'go', 'java', 'python'})

    def version_gte(self, another: str) -> bool:
        """Returns a bool for whether the version is >= another one.

        A version is greater than or equal to another version means its version
        number is greater than or equal to another version's number. Version
        "master" is always considered latest.
        E.g., master >= v1.41.x >= v1.40.x >= v1.9.x.
        """
        if self.version == 'master':
            return True
        return _parse_version(self.version) >= _parse_version(another)

    def version_lt(self, another: str) -> bool:
        """Returns a bool for whether the version is < another one.

        Version "master" is always considered latest.
        E.g., v1.9.x < v1.40.x < v1.41.x < master.
        """
        if self.version == 'master':
            return False
        return _parse_version(self.version) < _parse_version(another)

    @property
    @functools.lru_cache(None)
    def is_common_lang_client(self) -> bool:
        """Whether the client is one of the gRPC implementations following common
        release schema.
        """
        return self.client_lang in self.common_langs


def evaluate_test_config(check: Callable[[TestConfig], bool]) -> None:
    """Evaluates the test config check against Abseil flags."""
    # NOTE(lidiz) a manual skip mechanism is needed because absl/flags
    # cannot be used in the built-in test-skipping decorators. See the
    # official FAQs:
    # https://abseil.io/docs/python/guides/flags#faqs
    test_config = TestConfig(
        client_lang=_get_lang(xds_k8s_flags.CLIENT_IMAGE.value),
        server_lang=_get_lang(xds_k8s_flags.SERVER_IMAGE.value),
        version=xds_flags.TESTING_VERSION.value)
    if not check(test_config):
        raise unittest.SkipTest(f'Unsupported test config: {test_config}')
