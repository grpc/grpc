from typing import Any

from .. import Command

class rotate(Command):
    description: str
    user_options: Any
    boolean_options: Any
    match: Any
    dist_dir: Any
    keep: Any
    def initialize_options(self) -> None: ...
    def finalize_options(self) -> None: ...
    def run(self) -> None: ...
