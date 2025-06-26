from typing import ClassVar

from docutils import parsers

class Parser(parsers.Parser):
    config_section_dependencies: ClassVar[tuple[str, ...]]
