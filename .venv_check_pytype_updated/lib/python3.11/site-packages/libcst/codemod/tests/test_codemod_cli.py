# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#


import platform
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest import skipIf

from libcst._parser.entrypoints import is_native
from libcst.codemod import CodemodTest
from libcst.testing.utils import UnitTest


class TestCodemodCLI(UnitTest):
    # pyre-ignore - no idea why pyre is complaining about this
    @skipIf(platform.system() == "Windows", "Windows")
    def test_codemod_formatter_error_input(self) -> None:
        rlt = subprocess.run(
            [
                sys.executable,
                "-m",
                "libcst.tool",
                "codemod",
                "remove_unused_imports.RemoveUnusedImportsCommand",
                # `ArgumentParser.parse_known_args()`'s behavior dictates that options
                # need to go after instead of before the codemod command identifier.
                "--python-version",
                "3.6",
                str(Path(__file__).parent / "codemod_formatter_error_input.py.txt"),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if not is_native():
            self.assertIn(
                "ParserSyntaxError: Syntax Error @ 14:11.",
                rlt.stderr.decode("utf-8"),
            )
        else:
            self.assertIn(
                "error: cannot format -: Cannot parse for target version Python 3.6: 13:10:     async with AsyncExitStack() as stack:",
                rlt.stderr.decode("utf-8"),
            )

    def test_codemod_external(self) -> None:
        # Test running the NOOP command as an "external command"
        # against this very file.
        output = subprocess.check_output(
            [
                sys.executable,
                "-m",
                "libcst.tool",
                "codemod",
                "-x",  # external module
                "libcst.codemod.commands.noop.NOOPCommand",
                str(Path(__file__)),
            ],
            encoding="utf-8",
            stderr=subprocess.STDOUT,
        )
        assert "Finished codemodding 1 files!" in output

    def test_warning_messages_several_files(self) -> None:
        code = """
        def baz() -> str:
            return "{}: {}".format(*baz)
        """
        with tempfile.TemporaryDirectory() as tmpdir:
            p = Path(tmpdir)
            (p / "mod1.py").write_text(CodemodTest.make_fixture_data(code))
            (p / "mod2.py").write_text(CodemodTest.make_fixture_data(code))
            (p / "mod3.py").write_text(CodemodTest.make_fixture_data(code))
            output = subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "libcst.tool",
                    "codemod",
                    "convert_format_to_fstring.ConvertFormatStringCommand",
                    str(p),
                ],
                encoding="utf-8",
                stderr=subprocess.PIPE,
            )
            # Each module will generate a warning, so we should get 3 warnings in total
            self.assertIn(
                "- 3 warnings were generated.",
                output.stderr,
            )

    def test_matcher_decorators_multiprocessing(self) -> None:
        file_count = 5
        code = """
        def baz(): # type: int
            return 5
        """
        with tempfile.TemporaryDirectory() as tmpdir:
            p = Path(tmpdir)
            # Using more than chunksize=4 files to trigger multiprocessing
            for i in range(file_count):
                (p / f"mod{i}.py").write_text(CodemodTest.make_fixture_data(code))
            output = subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "libcst.tool",
                    "codemod",
                    # Good candidate since it uses matcher decorators
                    "convert_type_comments.ConvertTypeComments",
                    str(p),
                    "--jobs",
                    str(file_count),
                ],
                encoding="utf-8",
                stderr=subprocess.PIPE,
            )
            self.assertIn(
                f"Transformed {file_count} files successfully.",
                output.stderr,
            )
