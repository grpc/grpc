from logging import Logger
from typing import Any, Literal

from ._types import Reader, TagDict

logger: Logger

class IfdTag:
    printable: str
    tag: int
    field_type: int
    field_offset: int
    field_length: int
    values: Any  # either string, bytes or list of data items
    def __init__(self, printable: str, tag: int, field_type: int, values: Any, field_offset: int, field_length: int) -> None: ...

class ExifHeader:
    file_handle: Reader
    endian: Literal["I", "M"]
    offset: int
    fake_exif: bool
    strict: bool
    debug: bool
    detailed: bool
    truncate_tags: bool
    tags: dict[str, Any]
    def __init__(
        self,
        file_handle: Reader,
        endian: Literal["I", "M"],
        offset: int,
        fake_exif: bool,
        strict: bool,
        debug: bool = False,
        detailed: bool = True,
        truncate_tags: bool = True,
    ) -> None: ...
    def s2n(self, offset: int, length: int, signed: bool = False) -> int: ...
    def n2b(self, offset: int, length: int) -> bytes: ...
    def list_ifd(self) -> list[int]: ...
    def dump_ifd(
        self, ifd: int, ifd_name: str, tag_dict: TagDict | None = None, relative: int = 0, stop_tag: str = "UNDEF"
    ) -> None: ...
    def extract_tiff_thumbnail(self, thumb_ifd: int) -> None: ...
    def extract_jpeg_thumbnail(self) -> None: ...
    def decode_maker_note(self) -> None: ...
    def parse_xmp(self, xmp_bytes: bytes) -> None: ...
