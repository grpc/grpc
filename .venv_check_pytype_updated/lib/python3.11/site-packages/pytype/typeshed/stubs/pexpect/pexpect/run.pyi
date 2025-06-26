from _typeshed import FileDescriptorOrPath
from os import _Environ
from typing import AnyStr

from .spawnbase import _InputRePattern, _Logfile

def run(
    command: str,
    timeout: float | None = 30,
    withexitstatus: bool = False,
    events: list[tuple[_InputRePattern, AnyStr]] | dict[_InputRePattern, AnyStr] | None = None,
    extra_args: None = None,
    logfile: _Logfile | None = None,
    cwd: FileDescriptorOrPath | None = None,
    env: _Environ[str] | None = None,
    **kwargs,
) -> AnyStr | tuple[AnyStr, int]: ...
def runu(
    command: str,
    timeout: float | None = 30,
    withexitstatus: bool = False,
    events: list[tuple[_InputRePattern, AnyStr]] | dict[_InputRePattern, AnyStr] | None = None,
    extra_args: None = None,
    logfile: _Logfile | None = None,
    cwd: FileDescriptorOrPath | None = None,
    env: _Environ[str] | None = None,
    **kwargs,
) -> AnyStr | tuple[AnyStr, int]: ...
