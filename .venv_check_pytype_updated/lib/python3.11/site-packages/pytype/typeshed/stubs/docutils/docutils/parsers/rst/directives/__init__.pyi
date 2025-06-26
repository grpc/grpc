from _typeshed import Incomplete

from docutils.languages import _LanguageModule
from docutils.nodes import document
from docutils.parsers.rst import Directive
from docutils.utils import SystemMessage

def register_directive(name: str, directive: type[Directive]) -> None: ...
def directive(
    directive_name: str, language_module: _LanguageModule, document: document
) -> tuple[type[Directive] | None, list[SystemMessage]]: ...
def __getattr__(name: str) -> Incomplete: ...
