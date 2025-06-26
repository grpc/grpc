import io
from _typeshed import Incomplete
from typing_extensions import TypeAlias

NativeIO: TypeAlias = io.StringIO

class Verifier:
    ffi: Incomplete
    preamble: Incomplete
    flags: Incomplete
    kwds: Incomplete
    tmpdir: Incomplete
    sourcefilename: Incomplete
    modulefilename: Incomplete
    ext_package: Incomplete
    def __init__(
        self,
        ffi,
        preamble,
        tmpdir: Incomplete | None = None,
        modulename: Incomplete | None = None,
        ext_package: Incomplete | None = None,
        tag: str = "",
        force_generic_engine: bool = False,
        source_extension: str = ".c",
        flags: Incomplete | None = None,
        relative_to: Incomplete | None = None,
        **kwds,
    ) -> None: ...
    def write_source(self, file: Incomplete | None = None) -> None: ...
    def compile_module(self) -> None: ...
    def load_library(self): ...
    def get_module_name(self): ...
    def get_extension(self): ...
    def generates_python_module(self): ...
    def make_relative_to(self, kwds, relative_to): ...

def set_tmpdir(dirname) -> None: ...
def cleanup_tmpdir(tmpdir: Incomplete | None = None, keep_so: bool = False) -> None: ...
