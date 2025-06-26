# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Set

import libcst
from libcst._visitors import CSTVisitor
from libcst.helpers.paths import chdir
from libcst.metadata import FilePathProvider, FullRepoManager, MetadataWrapper
from libcst.testing.utils import UnitTest


class FilePathProviderTest(UnitTest):
    def setUp(self) -> None:
        self.td = TemporaryDirectory()
        self.tdp = Path(self.td.name).resolve()
        self.addCleanup(self.td.cleanup)

    def test_provider_cache(self) -> None:
        pkg = self.tdp / "pkg"
        pkg.mkdir()
        files = [Path(pkg / name) for name in ("file1.py", "file2.py", "file3.py")]
        [file.write_text("print('hello')\n") for file in files]

        with self.subTest("absolute paths"):
            repo_manager = FullRepoManager(
                self.tdp, [f.as_posix() for f in files], {FilePathProvider}
            )
            repo_manager.resolve_cache()

            expected = {
                FilePathProvider: {f.as_posix(): f for f in files},
            }
            self.assertDictEqual(expected, repo_manager.cache)

        with self.subTest("repo relative paths"):
            repo_manager = FullRepoManager(
                self.tdp,
                [f.relative_to(self.tdp).as_posix() for f in files],
                {FilePathProvider},
            )
            repo_manager.resolve_cache()

            expected = {
                FilePathProvider: {
                    f.relative_to(self.tdp).as_posix(): f for f in files
                },
            }
            self.assertDictEqual(expected, repo_manager.cache)

        with self.subTest("dot relative paths"):
            with chdir(self.tdp):
                repo_manager = FullRepoManager(
                    ".",
                    [f.relative_to(self.tdp).as_posix() for f in files],
                    {FilePathProvider},
                )
                repo_manager.resolve_cache()

                expected = {
                    FilePathProvider: {
                        f.relative_to(self.tdp).as_posix(): f for f in files
                    },
                }
                self.assertDictEqual(expected, repo_manager.cache)

    def test_visitor(self) -> None:
        pkg = self.tdp / "pkg"
        pkg.mkdir()
        files = [Path(pkg / name) for name in ("file1.py", "file2.py", "file3.py")]
        [file.write_text("print('hello')\n") for file in files]

        seen: Set[Path] = set()

        class FakeVisitor(CSTVisitor):
            METADATA_DEPENDENCIES = [FilePathProvider]

            def visit_Module(self, node: libcst.Module) -> None:
                seen.add(self.get_metadata(FilePathProvider, node))

        with self.subTest("absolute paths"):
            seen.clear()
            repo_manager = FullRepoManager(
                self.tdp, [f.as_posix() for f in files], {FilePathProvider}
            )
            repo_manager.resolve_cache()

            for file in files:
                module = libcst.parse_module(file.read_bytes())
                wrapper = MetadataWrapper(
                    module, cache=repo_manager.get_cache_for_path(file.as_posix())
                )
                wrapper.visit(FakeVisitor())

            expected = set(files)
            self.assertSetEqual(expected, seen)

        with self.subTest("repo relative paths"):
            seen.clear()
            repo_manager = FullRepoManager(
                self.tdp,
                [f.relative_to(self.tdp).as_posix() for f in files],
                {FilePathProvider},
            )
            repo_manager.resolve_cache()

            for file in files:
                module = libcst.parse_module(file.read_bytes())
                wrapper = MetadataWrapper(
                    module,
                    cache=repo_manager.get_cache_for_path(
                        file.relative_to(self.tdp).as_posix()
                    ),
                )
                wrapper.visit(FakeVisitor())

            expected = set(files)
            self.assertSetEqual(expected, seen)

        with self.subTest("dot relative paths"):
            with chdir(self.tdp):
                seen.clear()
                repo_manager = FullRepoManager(
                    ".",
                    [f.relative_to(self.tdp).as_posix() for f in files],
                    {FilePathProvider},
                )
                repo_manager.resolve_cache()

                for file in files:
                    module = libcst.parse_module(file.read_bytes())
                    wrapper = MetadataWrapper(
                        module,
                        cache=repo_manager.get_cache_for_path(
                            file.relative_to(self.tdp).as_posix()
                        ),
                    )
                    wrapper.visit(FakeVisitor())

                expected = set(files)
                self.assertSetEqual(expected, seen)
