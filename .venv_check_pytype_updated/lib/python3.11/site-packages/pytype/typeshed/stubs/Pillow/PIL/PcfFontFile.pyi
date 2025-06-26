from _typeshed import Incomplete

from .FontFile import FontFile

PCF_MAGIC: int
PCF_PROPERTIES: Incomplete
PCF_ACCELERATORS: Incomplete
PCF_METRICS: Incomplete
PCF_BITMAPS: Incomplete
PCF_INK_METRICS: Incomplete
PCF_BDF_ENCODINGS: Incomplete
PCF_SWIDTHS: Incomplete
PCF_GLYPH_NAMES: Incomplete
PCF_BDF_ACCELERATORS: Incomplete
BYTES_PER_ROW: Incomplete

def sz(s, o): ...

class PcfFontFile(FontFile):
    name: str
    charset_encoding: Incomplete
    toc: Incomplete
    fp: Incomplete
    info: Incomplete
    def __init__(self, fp, charset_encoding: str = "iso8859-1") -> None: ...
