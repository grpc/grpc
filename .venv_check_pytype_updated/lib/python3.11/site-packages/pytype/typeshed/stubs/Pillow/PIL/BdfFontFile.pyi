from _typeshed import Incomplete

from .FontFile import FontFile

bdf_slant: Incomplete
bdf_spacing: Incomplete

def bdf_char(f): ...

class BdfFontFile(FontFile):
    def __init__(self, fp) -> None: ...
