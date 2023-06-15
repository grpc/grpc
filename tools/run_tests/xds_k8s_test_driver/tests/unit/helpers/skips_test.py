# Copyright 2023 gRPC authors.
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
from typing import Optional, Sequence

from absl.testing import absltest
from absl.testing import parameterized

from packaging import version as pkg_version

from framework.helpers import skips

TestConfig = skips.TestConfig
Lang = skips.Lang


class TestConfigVersionGteTest(parameterized.TestCase):
    """Unit test for the version comparison helper version_gte."""

    INVALID_VERSIONS: tuple[str, ...] = (
        "1.2.xx",
        "1.x.alpha",
        "alpha",
        "1.x.3",
        "x.1.3",
        "v1.x.3",
    )

    VALID_VERSIONS: tuple[str, ...] = (
        "1.2.x",
        "1.2.0",
        "v1.2.x",
        "v1.2.0",
        "1.999.x",
        "2.888.xx",
    )

    @classmethod
    def _make_test_config(cls, version: str) -> TestConfig:
        return TestConfig(
            client_lang=Lang.UNKNOWN,
            server_lang=Lang.UNKNOWN,
            version=version,
        )

    @parameterized.product(
        version_left=("master", "dev", "dev-master", None),
        version_right=("master", "dev", "dev-master", "1.999.x", "dev-1.999.x"),
    )
    def test_left_always_gte_special_case(
        self,
        version_left: str,
        version_right: str,
    ):
        self.assertTrue(
            self._make_test_config(version_left).version_gte(version_right),
            msg=f"Expected {version_left} >= {version_right}",
        )

    @parameterized.parameters(
        ("dev-1.3.x", "1.2.x", True),
        ("dev-1.3.x", "1.3.x", True),
        ("dev-1.3.x", "1.4.x", False),
        ("dev-1.3.x", "master", False),
    )
    def test_left_dev_same_as_right_version(
        self,
        version_left: str,
        version_right: str,
        result: bool,
    ):
        self.assertEqual(
            self._make_test_config(version_left).version_gte(version_right),
            result,
            msg=f"Expected ({version_left} >= {version_right}) == {result}",
        )

    @parameterized.parameters(
        # Dev prefix is not allowed on the right side.
        "dev-1.3.x",
        "dev-1.3.0",
        "dev-master",
        *INVALID_VERSIONS,
    )
    def test_right_invalid_version(self, version_right: Optional[str]):
        test_config = self._make_test_config("1.42.x")
        with self.assertRaises(pkg_version.InvalidVersion):
            test_config.version_gte(version_right)

    @parameterized.parameters(*INVALID_VERSIONS)
    def test_left_invalid_version(self, version_left: Optional[str]):
        test_config = self._make_test_config(version_left)
        with self.assertRaises(pkg_version.InvalidVersion):
            test_config.version_gte("1.42.x")

    @parameterized.parameters(
        # Dev prefix is allowed on the right side.
        "dev-1.3.x",
        "dev-1.3.0",
        "dev-master",
        None,
        *VALID_VERSIONS,
    )
    def test_left_valid_version(self, version_left: Optional[str]):
        test_config = self._make_test_config(version_left)
        try:
            test_config.version_gte("1.42.x")
        except pkg_version.InvalidVersion as e:
            self.fail(
                f"Version on the left {version_left} expected to be valid"
            )

    @parameterized.parameters(*VALID_VERSIONS)
    def test_right_valid_version(self, version_right: Optional[str]):
        test_config = self._make_test_config("1.42.x")
        try:
            test_config.version_gte(version_right)
        except pkg_version.InvalidVersion as e:
            self.fail(
                f"Version on the right {version_right} expected to be valid"
            )


if __name__ == "__main__":
    absltest.main()
