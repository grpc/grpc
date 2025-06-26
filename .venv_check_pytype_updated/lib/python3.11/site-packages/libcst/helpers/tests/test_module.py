# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
from pathlib import Path, PurePath
from typing import Any, Optional
from unittest.mock import patch

import libcst as cst
from libcst.helpers.common import ensure_type
from libcst.helpers.module import (
    calculate_module_and_package,
    get_absolute_module_for_import,
    get_absolute_module_for_import_or_raise,
    get_absolute_module_from_package_for_import,
    get_absolute_module_from_package_for_import_or_raise,
    insert_header_comments,
    ModuleNameAndPackage,
)
from libcst.testing.utils import data_provider, UnitTest


class ModuleTest(UnitTest):
    def test_insert_header_comments(self) -> None:
        inserted_comments = ["# INSERT ME", "# AND ME"]
        comment_lines = ["# First comment", "# Another one", "# comment 3"]
        empty_lines = [" ", ""]
        non_header_line = ["SOME_VARIABLE = 0"]
        original_code = "\n".join(comment_lines + empty_lines + non_header_line)
        expected_code = "\n".join(
            comment_lines + inserted_comments + empty_lines + non_header_line
        )
        node = cst.parse_module(original_code)
        self.assertEqual(
            insert_header_comments(node, inserted_comments).code, expected_code
        )

        # No comment case
        original_code = "\n".join(empty_lines + non_header_line)
        expected_code = "\n".join(inserted_comments + empty_lines + non_header_line)
        node = cst.parse_module(original_code)
        self.assertEqual(
            insert_header_comments(node, inserted_comments).code, expected_code
        )

        # No empty lines case
        original_code = "\n".join(comment_lines + non_header_line)
        expected_code = "\n".join(comment_lines + inserted_comments + non_header_line)
        node = cst.parse_module(original_code)
        self.assertEqual(
            insert_header_comments(node, inserted_comments).code, expected_code
        )

        # Empty line between comments
        comment_lines.insert(1, " ")
        original_code = "\n".join(comment_lines + empty_lines + non_header_line)
        expected_code = "\n".join(
            comment_lines + inserted_comments + empty_lines + non_header_line
        )
        node = cst.parse_module(original_code)
        self.assertEqual(
            insert_header_comments(node, inserted_comments).code, expected_code
        )

        # No header case
        original_code = "\n".join(non_header_line)
        expected_code = "\n".join(inserted_comments + non_header_line)
        node = cst.parse_module(original_code)
        self.assertEqual(
            insert_header_comments(node, inserted_comments).code, expected_code
        )

    @data_provider(
        (
            # Simple imports that are already absolute.
            (None, "from a.b import c", "a.b"),
            ("x.y.z", "from a.b import c", "a.b"),
            # Relative import that can't be resolved due to missing module.
            (None, "from ..w import c", None),
            # Relative import that goes past the module level.
            ("x", "from ...y import z", None),
            ("x.y.z", "from .....w import c", None),
            ("x.y.z", "from ... import c", None),
            # Correct resolution of absolute from relative modules.
            ("x.y.z", "from . import c", "x.y"),
            ("x.y.z", "from .. import c", "x"),
            ("x.y.z", "from .w import c", "x.y.w"),
            ("x.y.z", "from ..w import c", "x.w"),
            ("x.y.z", "from ...w import c", "w"),
        )
    )
    def test_get_absolute_module(
        self,
        module: Optional[str],
        importfrom: str,
        output: Optional[str],
    ) -> None:
        node = ensure_type(cst.parse_statement(importfrom), cst.SimpleStatementLine)
        assert len(node.body) == 1, "Unexpected number of statements!"
        import_node = ensure_type(node.body[0], cst.ImportFrom)

        self.assertEqual(get_absolute_module_for_import(module, import_node), output)
        if output is None:
            with self.assertRaises(Exception):
                get_absolute_module_for_import_or_raise(module, import_node)
        else:
            self.assertEqual(
                get_absolute_module_for_import_or_raise(module, import_node), output
            )

    @data_provider(
        (
            # Simple imports that are already absolute.
            (None, "from a.b import c", "a.b"),
            ("x/y/z.py", "from a.b import c", "a.b"),
            ("x/y/z/__init__.py", "from a.b import c", "a.b"),
            # Relative import that can't be resolved due to missing module.
            (None, "from ..w import c", None),
            # Attempted relative import with no known parent package
            ("__init__.py", "from .y import z", None),
            ("x.py", "from .y import z", None),
            # Relative import that goes past the module level.
            ("x.py", "from ...y import z", None),
            ("x/y/z.py", "from ... import c", None),
            ("x/y/z.py", "from ...w import c", None),
            ("x/y/z/__init__.py", "from .... import c", None),
            ("x/y/z/__init__.py", "from ....w import c", None),
            # Correct resolution of absolute from relative modules.
            ("x/y/z.py", "from . import c", "x.y"),
            ("x/y/z.py", "from .. import c", "x"),
            ("x/y/z.py", "from .w import c", "x.y.w"),
            ("x/y/z.py", "from ..w import c", "x.w"),
            ("x/y/z/__init__.py", "from . import c", "x.y.z"),
            ("x/y/z/__init__.py", "from .. import c", "x.y"),
            ("x/y/z/__init__.py", "from ... import c", "x"),
            ("x/y/z/__init__.py", "from .w import c", "x.y.z.w"),
            ("x/y/z/__init__.py", "from ..w import c", "x.y.w"),
            ("x/y/z/__init__.py", "from ...w import c", "x.w"),
        )
    )
    def test_get_absolute_module_from_package(
        self,
        filename: Optional[str],
        importfrom: str,
        output: Optional[str],
    ) -> None:
        package = None
        if filename is not None:
            info = calculate_module_and_package(".", filename)
            package = info.package
        node = ensure_type(cst.parse_statement(importfrom), cst.SimpleStatementLine)
        assert len(node.body) == 1, "Unexpected number of statements!"
        import_node = ensure_type(node.body[0], cst.ImportFrom)

        self.assertEqual(
            get_absolute_module_from_package_for_import(package, import_node), output
        )
        if output is None:
            with self.assertRaises(Exception):
                get_absolute_module_from_package_for_import_or_raise(
                    package, import_node
                )
        else:
            self.assertEqual(
                get_absolute_module_from_package_for_import_or_raise(
                    package, import_node
                ),
                output,
            )

    @data_provider(
        (
            # Nodes without an asname
            (cst.ImportAlias(name=cst.Name("foo")), "foo", None),
            (
                cst.ImportAlias(name=cst.Attribute(cst.Name("foo"), cst.Name("bar"))),
                "foo.bar",
                None,
            ),
            # Nodes with an asname
            (
                cst.ImportAlias(
                    name=cst.Name("foo"), asname=cst.AsName(name=cst.Name("baz"))
                ),
                "foo",
                "baz",
            ),
            (
                cst.ImportAlias(
                    name=cst.Attribute(cst.Name("foo"), cst.Name("bar")),
                    asname=cst.AsName(name=cst.Name("baz")),
                ),
                "foo.bar",
                "baz",
            ),
        )
    )
    def test_importalias_helpers(
        self, alias_node: cst.ImportAlias, full_name: str, alias: Optional[str]
    ) -> None:
        self.assertEqual(alias_node.evaluated_name, full_name)
        self.assertEqual(alias_node.evaluated_alias, alias)

    @data_provider(
        (
            # Various files inside the root should give back valid modules.
            (
                "/home/username/root",
                "/home/username/root/file.py",
                ModuleNameAndPackage("file", ""),
            ),
            (
                "/home/username/root/",
                "/home/username/root/file.py",
                ModuleNameAndPackage("file", ""),
            ),
            (
                "/home/username/root/",
                "/home/username/root/some/dir/file.py",
                ModuleNameAndPackage("some.dir.file", "some.dir"),
            ),
            # Various special files inside the root should give back valid modules.
            (
                "/home/username/root/",
                "/home/username/root/some/dir/__init__.py",
                ModuleNameAndPackage("some.dir", "some.dir"),
            ),
            (
                "/home/username/root/",
                "/home/username/root/some/dir/__main__.py",
                ModuleNameAndPackage("some.dir", "some.dir"),
            ),
            (
                "c:/Program Files/",
                "c:/Program Files/some/dir/file.py",
                ModuleNameAndPackage("some.dir.file", "some.dir"),
            ),
            (
                "c:/Program Files/",
                "c:/Program Files/some/dir/__main__.py",
                ModuleNameAndPackage("some.dir", "some.dir"),
            ),
        ),
    )
    def test_calculate_module_and_package(
        self,
        repo_root: str,
        filename: str,
        module_and_package: Optional[ModuleNameAndPackage],
    ) -> None:
        self.assertEqual(
            calculate_module_and_package(repo_root, filename), module_and_package
        )

    @data_provider(
        (
            ("foo/foo/__init__.py", ModuleNameAndPackage("foo", "foo")),
            ("foo/foo/file.py", ModuleNameAndPackage("foo.file", "foo")),
            (
                "foo/foo/sub/subfile.py",
                ModuleNameAndPackage("foo.sub.subfile", "foo.sub"),
            ),
            ("libs/bar/bar/thing.py", ModuleNameAndPackage("bar.thing", "bar")),
            (
                "noproj/some/file.py",
                ModuleNameAndPackage("noproj.some.file", "noproj.some"),
            ),
        )
    )
    def test_calculate_module_and_package_using_pyproject_toml(
        self,
        rel_path: str,
        module_and_package: Optional[ModuleNameAndPackage],
    ) -> None:
        mock_tree: dict[str, Any] = {
            "home": {
                "user": {
                    "root": {
                        "foo": {
                            "pyproject.toml": "content",
                            "foo": {
                                "__init__.py": "content",
                                "file.py": "content",
                                "sub": {
                                    "subfile.py": "content",
                                },
                            },
                        },
                        "libs": {
                            "bar": {
                                "pyproject.toml": "content",
                                "bar": {
                                    "__init__.py": "content",
                                    "thing.py": "content",
                                },
                            }
                        },
                        "noproj": {
                            "some": {
                                "file.py": "content",
                            }
                        },
                    },
                },
            },
        }
        repo_root = Path("/home/user/root").resolve()
        fake_root: Path = repo_root.parent.parent.parent

        def mock_exists(path: PurePath) -> bool:
            parts = path.relative_to(fake_root).parts
            subtree = mock_tree
            for part in parts:
                if (subtree := subtree.get(part)) is None:
                    return False
            return True

        with patch("pathlib.Path.exists", new=mock_exists):
            self.assertEqual(
                calculate_module_and_package(
                    repo_root, repo_root / rel_path, use_pyproject_toml=True
                ),
                module_and_package,
            )

    @data_provider(
        (
            # Providing a file outside the root should raise an exception
            ("/home/username/root", "/some/dummy/file.py"),
            ("/home/username/root/", "/some/dummy/file.py"),
            ("/home/username/root", "/home/username/file.py"),
            # some windows tests
            (
                "c:/Program Files/",
                "d:/Program Files/some/dir/file.py",
            ),
            (
                "c:/Program Files/other/",
                "c:/Program Files/some/dir/file.py",
            ),
        )
    )
    def test_invalid_module_and_package(
        self,
        repo_root: str,
        filename: str,
    ) -> None:
        with self.assertRaises(ValueError):
            calculate_module_and_package(repo_root, filename)
