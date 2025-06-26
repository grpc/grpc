import logging
import pathlib

logger: logging.Logger

class PlaysoundException(Exception): ...

def playsound(sound: str | pathlib.Path, block: bool = ...) -> None: ...
