from typing import Any

from .._distutils.command import install_scripts as orig

class install_scripts(orig.install_scripts):
    no_ep: bool
    def initialize_options(self) -> None: ...
    outfiles: Any
    def run(self) -> None: ...
    def write_script(self, script_name, contents, mode: str = "t", *ignored) -> None: ...
