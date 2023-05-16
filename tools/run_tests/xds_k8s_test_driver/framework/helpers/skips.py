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
import enum
import logging
import re
from typing import Callable, Optional
import unittest

from packaging import version as pkg_version

from framework import xds_flags
from framework import xds_k8s_flags

logger = logging.getLogger(__name__)


class Lang(enum.Flag):
    UNKNOWN = enum.auto()
    CPP = enum.auto()
    GO = enum.auto()
    JAVA = enum.auto()
    PYTHON = enum.auto()
    NODE = enum.auto()

    def __str__(self):
        return str(self.name).lower()

    @classmethod
    def from_string(cls, lang: str):
        try:
            return cls[lang.upper()]
        except KeyError:
            return cls.UNKNOWN


@dataclass
class TestConfig:
    """Describes the config for the test suite.

    TODO(sergiitk): rename to LangSpec and rename skips.py to lang.py.
    """
    client_lang: Lang
    server_lang: Lang
    version: Optional[str]

    def version_gte(self, another: str) -> bool:
        """Returns a bool for whether this VERSION is >= then ANOTHER version.

        Special cases:

        1) Versions "master" or "dev" are always greater than ANOTHER:
        - master > v1.999.x > v1.55.x
        - dev > v1.999.x > v1.55.x
        - dev == master

        2) Versions "dev-VERSION" behave the same as the VERSION:
        - dev-master > v1.999.x > v1.55.x
        - dev-master == dev == master
        - v1.55.x > dev-v1.54.x > v1.53.x
        - dev-v1.54.x == v1.54.x

        3) Unspecified version (self.version is None) is treated as "master".
        """
        if self.version in ('master', 'dev', 'dev-master', None):
            return True
        if another == 'master':
            return False
        return self._parse_version(self.version) >= self._parse_version(another)

    def __str__(self):
        return (f"TestConfig(client_lang='{self.client_lang}', "
                f"server_lang='{self.server_lang}', version={self.version!r})")

    @staticmethod
    def _parse_version(version: str) -> pkg_version.Version:
        if version.startswith('dev-'):
            # Treat "dev-VERSION" as "VERSION".
            version = version[4:]
        if version.endswith('.x'):
            version = version[:-2]
        return pkg_version.Version(version)


def _get_lang(image_name: str) -> Lang:
    return Lang.from_string(
        re.search(r'/(\w+)-(client|server):', image_name).group(1))


def evaluate_test_config(check: Callable[[TestConfig], bool]) -> TestConfig:
    """Evaluates the test config check against Abseil flags.

    TODO(sergiitk): split into parse_lang_spec and check_is_supported.
    """
    # NOTE(lidiz) a manual skip mechanism is needed because absl/flags
    # cannot be used in the built-in test-skipping decorators. See the
    # official FAQs:
    # https://abseil.io/docs/python/guides/flags#faqs
    test_config = TestConfig(
        client_lang=_get_lang(xds_k8s_flags.CLIENT_IMAGE.value),
        server_lang=_get_lang(xds_k8s_flags.SERVER_IMAGE.value),
        version=xds_flags.TESTING_VERSION.value)
    if not check(test_config):
        logger.info('Skipping %s', test_config)
        raise unittest.SkipTest(f'Unsupported test config: {test_config}')

    logger.info('Detected language and version: %s', test_config)
    return test_config
