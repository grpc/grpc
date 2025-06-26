# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


import json
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Tuple, Union

import libcst as cst
from libcst.metadata import MetadataWrapper, PositionProvider
from libcst.metadata.type_inference_provider import PyreData
from libcst.testing.utils import data_provider, UnitTest

TEST_SUITE_PATH: Path = Path(__file__).parent / "pyre"


class TypeVerificationVisitor(cst.CSTVisitor):
    METADATA_DEPENDENCIES = (PositionProvider,)

    def __init__(
        self, lookup: Mapping[Tuple[int, int, int, int], str], test: UnitTest
    ) -> None:
        self.lookup = lookup
        self.test = test
        self.attributes: List[cst.Attribute] = []  # stack of Attribute
        self.imports: List[Union[cst.Import, cst.ImportFrom]] = []  # stack of imports
        self.annotations: List[cst.Annotation] = []  # stack of Annotation
        super().__init__()

    def visit_Attribute(self, node: cst.Attribute) -> Optional[bool]:
        pos = self.get_metadata(PositionProvider, node)
        start = pos.start
        end = pos.end
        self.attributes.append(node)
        tup = (start.line, start.column, end.line, end.column)

        # remove this if condition when the type issues are fixed.
        self.test.assertIn(
            tup,
            self.lookup,
            f"Attribute node {node} at {tup} found without inferred type.",
        )

    def leave_Attribute(self, original_node: cst.Attribute) -> None:
        self.attributes.pop()

    def visit_Name(self, node: cst.Name) -> Optional[bool]:
        if (
            len(self.imports) > 0
            or len(self.attributes) > 0
            or len(self.annotations) > 0
        ):
            return
        pos = self.get_metadata(PositionProvider, node)
        start = pos.start
        end = pos.end
        tup = (start.line, start.column, end.line, end.column)
        # remove this if condition when the type issues are fixed.
        if node.value not in {"n", "i"}:
            self.test.assertIn(
                tup,
                self.lookup,
                f"Name node {node.value} at {tup} found without inferred type.",
            )

    def visit_Import(self, node: cst.Import) -> Optional[bool]:
        self.imports.append(node)

    def leave_Import(self, original_node: cst.Import) -> None:
        self.imports.pop()

    def visit_ImportFrom(self, node: cst.ImportFrom) -> Optional[bool]:
        self.imports.append(node)

    def leave_ImportFrom(self, original_node: cst.ImportFrom) -> None:
        self.imports.pop()

    def visit_Annotation(self, node: cst.Annotation) -> Optional[bool]:
        self.annotations.append(node)

    def leave_Annotation(self, original_node: cst.Annotation) -> None:
        self.annotations.pop()


class PyreIntegrationTest(UnitTest):
    # pyre-fixme[56]: Pyre was not able to infer the type of argument
    #  `comprehension((source_path, data_path) for generators(generator((source_path,
    #  data_path) in zip(TEST_SUITE_PATH.glob("*.py"), TEST_SUITE_PATH.glob("*.json"))
    #  if )))` to decorator factory `libcst.testing.utils.data_provider`.
    @data_provider(
        (
            (source_path, data_path)
            for source_path, data_path in zip(
                TEST_SUITE_PATH.glob("*.py"), TEST_SUITE_PATH.glob("*.json")
            )
        )
    )
    def test_type_availability(self, source_path: Path, data_path: Path) -> None:
        module = cst.parse_module(source_path.read_text())
        data: PyreData = json.loads(data_path.read_text())
        lookup: Dict[Tuple[int, int, int, int], str] = {}
        for t in data["types"]:
            loc = t["location"]
            start = loc["start"]
            stop = loc["stop"]
            lookup[(start["line"], start["column"], stop["line"], stop["column"])] = t[
                "annotation"
            ]
        MetadataWrapper(module).visit(TypeVerificationVisitor(lookup, self))


if __name__ == "__main__":
    import sys

    print("run `scripts/regenerate-fixtures.py` instead")
    sys.exit(1)
