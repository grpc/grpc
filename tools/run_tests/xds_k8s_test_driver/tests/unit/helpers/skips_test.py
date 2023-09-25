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
from typing import Optional

from absl.testing import absltest
from absl.testing import parameterized
from packaging import version as pkg_version

from framework.helpers import skips


class TestConfigVersionGteTest(parameterized.TestCase):
    """
    Unit test for the version comparison helper TestConfig.version_gte.

    See TestConfig.version_gte for doc for the description of the special cases
    mentioned below.
    """

    INVALID_VERSIONS: tuple[str, ...] = (
        "1.2.xx",
        "1.x.x",
        "alpha",
        "1.x.alpha",
        "1.2.x.alpha",
        "1.x.3",
        "x.1.3",
        "v1.x.3",
        "v.1.2.0",
        "y1.2.0",
    )

    VALID_VERSIONS: tuple[str, ...] = (
        "1.2.x",
        "1.2.0",
        "v1.2.x",
        "v1.2.0",
        "V1.2.0",
        "1.999.x",
        "2.999.x",
    )

    VALID_DEV_VERSIONS: tuple[str, ...] = (
        "dev-1.3.x",
        "dev-1.3.0",
        "dev-master",
    )

    @classmethod
    def _make_test_config(cls, version: str) -> skips.TestConfig:
        return skips.TestConfig(
            client_lang=skips.Lang.UNKNOWN,
            server_lang=skips.Lang.UNKNOWN,
            version=version,
        )

    @classmethod
    def _v_prefix_combos(cls, left: str, right: str) -> tuple[str, str]:
        yield left, right
        yield f"v{left}", right
        yield left, f"v{right}"
        yield f"v{left}", f"v{right}"

    @parameterized.parameters(
        # Greater than.
        ("1.3.0", "1.2.0", True),
        ("1.3.1", "1.3.0", True),
        ("2.0.0", "1.999.999", True),
        ("2.0", "1.999.999", True),
        # Equal.
        ("1.3.0", "1.3.0", True),
        ("1.3.0", "1.3", True),
        ("1.3.1", "1.3.1", True),
        # Less than.
        ("1.2.0", "1.3.0", False),
        ("1.2.0", "1.2.1", False),
        ("1.999.999", "2.0.0", False),
        ("1.999.999", "2.0", False),
    )
    def test_basic_parse_and_compare(
        self,
        version_left: str,
        version_right: str,
        result: bool,
    ):
        """Verifies basic parsing and comparison."""
        self.assertEqual(
            self._make_test_config(version_left).version_gte(version_right),
            result,
            msg=f"Expected ({version_left} >= {version_right}) == {result}",
        )

    @parameterized.parameters(
        ("1.3.x", "1.2.x", True),  # Greater than.
        ("1.3.x", "1.3.x", True),  # Equal.
        ("1.3.x", "1.4.x", False),  # Less than.
    )
    def test_v_prefix(
        self,
        version_left: str,
        version_right: str,
        result: bool,
    ):
        """Verifies the "v" prefix is allowed."""
        for left, right in self._v_prefix_combos(version_left, version_right):
            self.assertEqual(
                self._make_test_config(left).version_gte(right),
                result,
                msg=f"Expected ({left} >= {right}) == {result}",
            )

    @parameterized.parameters(
        # Greater than.
        ("1.3.x", "1.2.9", True),
        ("1.3.1", "1.3.x", True),
        ("1.3.x", "1.2.x", True),
        ("2.0.x", "1.999.x", True),
        # Equal.
        ("1.3.x", "1.3.x", True),
        ("1.3.0", "1.3.x", True),
        ("1.3.x", "1.3.0", True),
        # Less than
        ("1.3.x", "1.3.1", False),
        ("1.3.x", "1.4.x", False),
        ("1.3.x", "1.4.0", False),
    )
    def test_x_in_version_patch(
        self,
        version_left: str,
        version_right: str,
        result: bool,
    ):
        """Verifies the patch-level "x" is equivalent to patch-level "0"."""
        self.assertEqual(
            self._make_test_config(version_left).version_gte(version_right),
            result,
            msg=f"Expected ({version_left} >= {version_right}) == {result}",
        )

    @parameterized.product(
        version_left=("master", "dev", "dev-master", None),
        version_right=("master", "dev", "dev-master", "1.999.x", "dev-1.999.x"),
    )
    def test_special_case_left_always_gte(
        self,
        version_left: str,
        version_right: str,
    ):
        """
        Special case 1: Versions "master", "dev", "dev-master" on the left
        are always "greater or equal" than any version on the right.
        """
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
    def test_special_case_left_dev_prefix_ignored(
        self,
        version_left: str,
        version_right: str,
        result: bool,
    ):
        """
        Special case 2: Versions "dev-VERSION" on the left behave the same
        as the VERSION on the right.
        """
        self.assertEqual(
            self._make_test_config(version_left).version_gte(version_right),
            result,
            msg=f"Expected ({version_left} >= {version_right}) == {result}",
        )

    @parameterized.parameters(
        *INVALID_VERSIONS,
        *VALID_DEV_VERSIONS,
    )
    def test_right_invalid_version(self, version_right: Optional[str]):
        """
        Verifies that invalid versions on the right raise InvalidVersion.

        Note that special case 2, versions "dev-VERSION", are not allowed
        on the right.
        """
        test_config = self._make_test_config("1.42.x")
        with self.assertRaises(pkg_version.InvalidVersion):
            test_config.version_gte(version_right)

    @parameterized.parameters(*INVALID_VERSIONS)
    def test_left_invalid_version(self, version_left: Optional[str]):
        """
        Verifies that invalid versions on the left raise InvalidVersion.
        """
        test_config = self._make_test_config(version_left)
        with self.assertRaises(pkg_version.InvalidVersion):
            test_config.version_gte("1.42.x")

    @parameterized.parameters(
        *VALID_VERSIONS,
        *VALID_DEV_VERSIONS,
        None,
    )
    def test_left_valid_version(self, version_left: Optional[str]):
        """
        Verifies that valid versions on the left don't raise InvalidVersion.
        """
        test_config = self._make_test_config(version_left)
        try:
            test_config.version_gte("1.42.x")
        except pkg_version.InvalidVersion as e:
            self.fail(
                f"Version on the left {version_left} expected to be valid"
            )

    @parameterized.parameters(*VALID_VERSIONS)
    def test_right_valid_version(self, version_right: Optional[str]):
        """
        Verifies that valid versions on the right don't raise InvalidVersion.
        """
        test_config = self._make_test_config("1.42.x")
        try:
            test_config.version_gte(version_right)
        except pkg_version.InvalidVersion as e:
            self.fail(
                f"Version on the right {version_right} expected to be valid"
            )


if __name__ == "__main__":
    absltest.main()
