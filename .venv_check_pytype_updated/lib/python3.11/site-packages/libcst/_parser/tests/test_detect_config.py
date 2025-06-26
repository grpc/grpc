# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from typing import Union

from libcst._parser.detect_config import detect_config
from libcst._parser.parso.utils import PythonVersionInfo
from libcst._parser.types.config import (
    parser_config_asdict,
    ParserConfig,
    PartialParserConfig,
)
from libcst.testing.utils import data_provider, UnitTest


class TestDetectConfig(UnitTest):
    @data_provider(
        {
            "empty_input": {
                "source": b"",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["\n", ""],
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=False,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "detect_trailing_newline_disabled": {
                "source": b"",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": False,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=[""],  # the trailing newline isn't inserted
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=False,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "detect_default_newline_disabled": {
                "source": b"pass\r",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": False,
                "detect_default_newline": False,
                "expected_config": ParserConfig(
                    lines=["pass\r", ""],  # the trailing newline isn't inserted
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=False,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "newline_inferred": {
                "source": b"first_line\r\n\nsomething\n",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["first_line\r\n", "\n", "something\n", ""],
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\r\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "newline_partial_given": {
                "source": b"first_line\r\nsecond_line\r\n",
                "partial": PartialParserConfig(
                    default_newline="\n", python_version="3.7"
                ),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["first_line\r\n", "second_line\r\n", ""],
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",  # The given partial disables inference
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "indent_inferred": {
                "source": b"if test:\n\t  something\n",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["if test:\n", "\t  something\n", ""],
                    encoding="utf-8",
                    default_indent="\t  ",
                    default_newline="\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "indent_partial_given": {
                "source": b"if test:\n\t  something\n",
                "partial": PartialParserConfig(
                    default_indent="      ", python_version="3.7"
                ),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["if test:\n", "\t  something\n", ""],
                    encoding="utf-8",
                    default_indent="      ",
                    default_newline="\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "encoding_inferred": {
                "source": b"#!/usr/bin/python3\n# -*- coding: latin-1 -*-\npass\n",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=[
                        "#!/usr/bin/python3\n",
                        "# -*- coding: latin-1 -*-\n",
                        "pass\n",
                        "",
                    ],
                    encoding="iso-8859-1",  # this is an alias for latin-1
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "encoding_partial_given": {
                "source": b"#!/usr/bin/python3\n# -*- coding: latin-1 -*-\npass\n",
                "partial": PartialParserConfig(
                    encoding="us-ascii", python_version="3.7"
                ),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=[
                        "#!/usr/bin/python3\n",
                        "# -*- coding: latin-1 -*-\n",
                        "pass\n",
                        "",
                    ],
                    encoding="us-ascii",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "encoding_str_not_bytes_disables_inference": {
                "source": "#!/usr/bin/python3\n# -*- coding: latin-1 -*-\npass\n",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=[
                        "#!/usr/bin/python3\n",
                        "# -*- coding: latin-1 -*-\n",
                        "pass\n",
                        "",
                    ],
                    encoding="utf-8",  # because source is a str, don't infer latin-1
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "encoding_non_ascii_compatible_utf_16_with_bom": {
                "source": b"\xff\xfet\x00e\x00s\x00t\x00",
                "partial": PartialParserConfig(encoding="utf-16", python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["test\n", ""],
                    encoding="utf-16",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=False,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "detect_trailing_newline_missing_newline": {
                "source": b"test",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["test\n", ""],
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=False,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "detect_trailing_newline_has_newline": {
                "source": b"test\n",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["test\n", ""],
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "detect_trailing_newline_missing_newline_after_line_continuation": {
                "source": b"test\\\n",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["test\\\n", "\n", ""],
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=False,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "detect_trailing_newline_has_newline_after_line_continuation": {
                "source": b"test\\\n\n",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=["test\\\n", "\n", ""],
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset(),
                ),
            },
            "future_imports_in_correct_position": {
                "source": b"# C\n''' D '''\nfrom __future__ import a as b\n",
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=[
                        "# C\n",
                        "''' D '''\n",
                        "from __future__ import a as b\n",
                        "",
                    ],
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset({"a"}),
                ),
            },
            "future_imports_in_mixed_position": {
                "source": (
                    b"from __future__ import a, b\nimport os\n"
                    + b"from __future__ import c\n"
                ),
                "partial": PartialParserConfig(python_version="3.7"),
                "detect_trailing_newline": True,
                "detect_default_newline": True,
                "expected_config": ParserConfig(
                    lines=[
                        "from __future__ import a, b\n",
                        "import os\n",
                        "from __future__ import c\n",
                        "",
                    ],
                    encoding="utf-8",
                    default_indent="    ",
                    default_newline="\n",
                    has_trailing_newline=True,
                    version=PythonVersionInfo(3, 7),
                    future_imports=frozenset({"a", "b"}),
                ),
            },
        }
    )
    def test_detect_module_config(
        self,
        *,
        source: Union[str, bytes],
        partial: PartialParserConfig,
        detect_trailing_newline: bool,
        detect_default_newline: bool,
        expected_config: ParserConfig,
    ) -> None:
        self.assertEqual(
            parser_config_asdict(
                detect_config(
                    source,
                    partial=partial,
                    detect_trailing_newline=detect_trailing_newline,
                    detect_default_newline=detect_default_newline,
                ).config
            ),
            parser_config_asdict(expected_config),
        )
