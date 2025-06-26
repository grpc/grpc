from _typeshed import Incomplete
from abc import abstractmethod
from collections.abc import Callable, Iterable
from typing import ClassVar
from typing_extensions import Self

from .dist import Distribution

class Command:
    distribution: Distribution
    sub_commands: ClassVar[list[tuple[str, Callable[[Self], bool] | None]]]
    def __init__(self, dist: Distribution) -> None: ...
    def ensure_finalized(self) -> None: ...
    @abstractmethod
    def initialize_options(self) -> None: ...
    @abstractmethod
    def finalize_options(self) -> None: ...
    @abstractmethod
    def run(self) -> None: ...
    def announce(self, msg: str, level: int = ...) -> None: ...
    def debug_print(self, msg: str) -> None: ...
    def ensure_string(self, option: str, default: str | None = ...) -> None: ...
    def ensure_string_list(self, option: str | list[str]) -> None: ...
    def ensure_filename(self, option: str) -> None: ...
    def ensure_dirname(self, option: str) -> None: ...
    def get_command_name(self) -> str: ...
    def set_undefined_options(self, src_cmd: str, *option_pairs: tuple[str, str]) -> None: ...
    def get_finalized_command(self, command: str, create: int = ...) -> Command: ...
    def reinitialize_command(self, command: Command | str, reinit_subcommands: int = ...) -> Command: ...
    def run_command(self, command: str) -> None: ...
    def get_sub_commands(self) -> list[str]: ...
    def warn(self, msg: str) -> None: ...
    def execute(
        self, func: Callable[..., object], args: Iterable[Incomplete], msg: str | None = ..., level: int = ...
    ) -> None: ...
    def mkpath(self, name: str, mode: int = ...) -> None: ...
    def copy_file(
        self,
        infile: str,
        outfile: str,
        preserve_mode: int = ...,
        preserve_times: int = ...,
        link: str | None = ...,
        level: int = ...,
    ) -> tuple[str, bool]: ...  # level is not used
    def copy_tree(
        self,
        infile: str,
        outfile: str,
        preserve_mode: int = ...,
        preserve_times: int = ...,
        preserve_symlinks: int = ...,
        level: int = ...,
    ) -> list[str]: ...  # level is not used
    def move_file(self, src: str, dst: str, level: int = ...) -> str: ...  # level is not used
    def spawn(self, cmd: Iterable[str], search_path: int = ..., level: int = ...) -> None: ...  # level is not used
    def make_archive(
        self,
        base_name: str,
        format: str,
        root_dir: str | None = ...,
        base_dir: str | None = ...,
        owner: str | None = ...,
        group: str | None = ...,
    ) -> str: ...
    def make_file(
        self,
        infiles: str | list[str] | tuple[str, ...],
        outfile: str,
        func: Callable[..., object],
        args: list[Incomplete],
        exec_msg: str | None = ...,
        skip_msg: str | None = ...,
        level: int = ...,
    ) -> None: ...  # level is not used
