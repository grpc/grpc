from logging import Logger

from ._types import Reader

logger: Logger

def find_jpeg_exif(fh: Reader, data: bytes, fake_exif: bool) -> tuple[int, bytes, bool]: ...
