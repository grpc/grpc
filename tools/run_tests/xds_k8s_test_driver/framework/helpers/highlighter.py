# Copyright 2021 gRPC authors.
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
"""The module contains helpers to enable color output in terminals.

Use this to log resources dumped as a structured document (f.e. YAML),
and enable colorful syntax highlighting.

TODO(sergiitk): This can be used to output protobuf responses formatted as JSON.
"""
import logging
from typing import Optional

from absl import flags
import pygments
import pygments.formatter
import pygments.formatters.other
import pygments.formatters.terminal
import pygments.formatters.terminal256
import pygments.lexer
import pygments.lexers.data
import pygments.styles

# The style for terminals supporting 8/16 colors.
STYLE_ANSI_16 = "ansi16"
# Join with pygments styles for terminals supporting 88/256 colors.
ALL_COLOR_STYLES = [STYLE_ANSI_16] + list(pygments.styles.get_all_styles())

# Flags.
COLOR = flags.DEFINE_bool("color", default=True, help="Colorize the output")
COLOR_STYLE = flags.DEFINE_enum(
    "color_style",
    default="material",
    enum_values=ALL_COLOR_STYLES,
    help=(
        "Color styles for terminals supporting 256 colors. "
        f"Use {STYLE_ANSI_16} style for terminals supporting 8/16 colors"
    ),
)

logger = logging.getLogger(__name__)

# Type aliases.
Lexer = pygments.lexer.Lexer
YamlLexer = pygments.lexers.data.YamlLexer
Formatter = pygments.formatter.Formatter
NullFormatter = pygments.formatters.other.NullFormatter
TerminalFormatter = pygments.formatters.terminal.TerminalFormatter
Terminal256Formatter = pygments.formatters.terminal256.Terminal256Formatter


class Highlighter:
    formatter: Formatter
    lexer: Lexer
    color: bool
    color_style: Optional[str] = None

    def __init__(
        self,
        *,
        lexer: Lexer,
        color: Optional[bool] = None,
        color_style: Optional[str] = None,
    ):
        self.lexer = lexer
        self.color = color if color is not None else COLOR.value

        if self.color:
            color_style = color_style if color_style else COLOR_STYLE.value
            if color_style not in ALL_COLOR_STYLES:
                raise ValueError(
                    f"Unrecognized color style {color_style}, "
                    f"valid styles: {ALL_COLOR_STYLES}"
                )
            if color_style == STYLE_ANSI_16:
                # 8/16 colors support only.
                self.formatter = TerminalFormatter()
            else:
                # 88/256 colors.
                self.formatter = Terminal256Formatter(style=color_style)
        else:
            self.formatter = NullFormatter()

    def highlight(self, code: str) -> str:
        return pygments.highlight(code, self.lexer, self.formatter)


class HighlighterYaml(Highlighter):
    def __init__(
        self, *, color: Optional[bool] = None, color_style: Optional[str] = None
    ):
        super().__init__(
            lexer=YamlLexer(encoding="utf-8"),
            color=color,
            color_style=color_style,
        )
