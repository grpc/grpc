from docutils.parsers.rst import Directive

MODULEDOC: str
LEXERDOC: str
FMTERDOC: str
FILTERDOC: str

class PygmentsDoc(Directive):
    filenames: set[str]
    def document_lexers(self) -> str: ...
    def document_formatters(self) -> str: ...
    def document_filters(self) -> str: ...

def setup(app) -> None: ...
