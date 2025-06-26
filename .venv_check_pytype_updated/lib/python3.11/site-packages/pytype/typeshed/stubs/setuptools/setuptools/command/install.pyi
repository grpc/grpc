from typing import Any

from .._distutils.command import install as orig

class install(orig.install):
    user_options: Any
    boolean_options: Any
    new_commands: Any
    old_and_unmanageable: Any
    single_version_externally_managed: Any
    def initialize_options(self) -> None: ...
    def finalize_options(self) -> None: ...
    path_file: Any
    extra_dirs: str
    def handle_extra_path(self): ...
    def run(self): ...
    def do_egg_install(self) -> None: ...
