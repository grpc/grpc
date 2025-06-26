# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import json
from pathlib import Path
from unittest.mock import Mock, patch

from libcst.metadata.full_repo_manager import FullRepoManager
from libcst.metadata.tests.test_type_inference_provider import _test_simple_class_helper
from libcst.metadata.type_inference_provider import TypeInferenceProvider
from libcst.testing.utils import UnitTest

REPO_ROOT_DIR: str = str(Path(__file__).parent.parent.parent.resolve())


class FullRepoManagerTest(UnitTest):
    @patch.object(TypeInferenceProvider, "gen_cache")
    def test_get_metadata_wrapper_with_empty_cache(self, gen_cache: Mock) -> None:
        path = "tests/pyre/simple_class.py"
        gen_cache.return_value = {path: {"types": []}}
        manager = FullRepoManager(REPO_ROOT_DIR, [path], [TypeInferenceProvider])
        wrapper = manager.get_metadata_wrapper_for_path(path)
        self.assertEqual(wrapper.resolve(TypeInferenceProvider), {})

    @patch.object(TypeInferenceProvider, "gen_cache")
    def test_get_metadata_wrapper_with_patched_cache(self, gen_cache: Mock) -> None:
        path_prefix = "tests/pyre/simple_class"
        path = f"{path_prefix}.py"
        gen_cache.return_value = {
            path: json.loads((Path(REPO_ROOT_DIR) / f"{path_prefix}.json").read_text())
        }
        manager = FullRepoManager(REPO_ROOT_DIR, [path], [TypeInferenceProvider])
        wrapper = manager.get_metadata_wrapper_for_path(path)
        _test_simple_class_helper(self, wrapper)

    @patch.object(TypeInferenceProvider, "gen_cache")
    def test_get_metadata_wrapper_with_invalid_path(self, gen_cache: Mock) -> None:
        path = "tests/pyre/simple_class.py"
        gen_cache.return_value = {path: {"types": []}}
        manager = FullRepoManager(
            REPO_ROOT_DIR, ["invalid_path.py"], [TypeInferenceProvider]
        )
        with self.assertRaisesRegex(
            Exception,
            "The path needs to be in paths parameter when constructing FullRepoManager for efficient batch processing.",
        ):
            manager.get_metadata_wrapper_for_path(path)

    @patch.object(TypeInferenceProvider, "gen_cache")
    def test_get_full_repo_cache(self, gen_cache: Mock) -> None:
        path_prefix = "tests/pyre/simple_class"
        path = f"{path_prefix}.py"
        mock_cache = {
            path: json.loads((Path(REPO_ROOT_DIR) / f"{path_prefix}.json").read_text())
        }
        gen_cache.return_value = mock_cache
        manager = FullRepoManager(REPO_ROOT_DIR, path, [TypeInferenceProvider])
        cache = manager.cache
        self.assertEqual(cache, {TypeInferenceProvider: mock_cache})
