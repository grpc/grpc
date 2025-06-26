import contextlib
import os
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Dict, Generator
from unittest import TestCase

from libcst import BaseExpression, Call, matchers as m, Name
from libcst.codemod import (
    CodemodContext,
    gather_files,
    parallel_exec_transform_with_prettyprint,
    VisitorBasedCodemodCommand,
)
from libcst.codemod.visitors import AddImportsVisitor


class PrintToPPrintCommand(VisitorBasedCodemodCommand):
    def __init__(self, context: CodemodContext, **kwargs: Dict[str, object]) -> None:
        super().__init__(context, **kwargs)
        self.context.scratch["PPRINT_WAS_HERE"] = True

    def leave_Call(self, original_node: Call, updated_node: Call) -> BaseExpression:
        if not self.context.scratch["PPRINT_WAS_HERE"]:
            raise AssertionError("Scratch space lost")

        if m.matches(updated_node, m.Call(func=m.Name("print"))):
            AddImportsVisitor.add_needed_import(
                self.context,
                "pprint",
                "pprint",
            )
            return updated_node.with_changes(func=Name("pprint"))
        return super().leave_Call(original_node, updated_node)


@contextlib.contextmanager
def temp_workspace() -> Generator[Path, None, None]:
    cwd = os.getcwd()
    with TemporaryDirectory() as temp_dir:
        try:
            ws = Path(temp_dir).resolve()
            os.chdir(ws)
            yield ws
        finally:
            os.chdir(cwd)


class ToolE2ETest(TestCase):
    def test_leaky_codemod(self) -> None:
        for msg, command in [
            ("instantiated", PrintToPPrintCommand(CodemodContext())),
            ("class", PrintToPPrintCommand),
        ]:
            with self.subTest(msg), temp_workspace() as tmp:
                # File to trigger codemod
                example: Path = tmp / "example.py"
                example.write_text("""print("Hello")""")
                # File that should not be modified
                other = tmp / "other.py"
                other.touch()
                # Just a dir named "dir.py", should be ignored
                adir = tmp / "dir.py"
                adir.mkdir()

                # Run command
                files = gather_files(".")
                result = parallel_exec_transform_with_prettyprint(
                    command,
                    files,
                    format_code=False,
                    hide_progress=True,
                )

                print(result)

                # Check results
                self.assertEqual(2, result.successes)
                self.assertEqual(0, result.skips)
                self.assertEqual(0, result.failures)
                # Expect example.py to be modified
                self.assertIn(
                    "from pprint import pprint",
                    example.read_text(),
                    "import missing in example.py",
                )
                # Expect other.py to NOT be modified
                self.assertNotIn(
                    "from pprint import pprint",
                    other.read_text(),
                    "import found in other.py",
                )
