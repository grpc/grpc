from enum import Enum, Flag, IntEnum, IntFlag
from typing import Literal
from typing_extensions import Self

from .syntax import Name

class SignatureFlag(IntEnum):
    SIGNATURES_EXIST: int
    APPEND_ONLY: int

class CoerciveEnum(Enum):
    @classmethod
    def coerce(cls, value: Self | str) -> Self: ...

class CoerciveIntEnum(IntEnum):
    @classmethod
    def coerce(cls, value: Self | str | int) -> Self: ...

class CoerciveIntFlag(IntFlag):
    @classmethod
    def coerce(cls, value: Self | str | int) -> Self: ...

class WrapMode(CoerciveEnum):
    WORD: str
    CHAR: str

class CharVPos(CoerciveEnum):
    SUP: str
    SUB: str
    NOM: str
    DENOM: str
    LINE: str

class Align(CoerciveEnum):
    C: str
    X: str
    L: str
    R: str
    J: str

class VAlign(CoerciveEnum):
    M: str
    T: str
    B: str

class TextEmphasis(CoerciveIntFlag):
    B: int
    I: int
    U: int

    @property
    def style(self) -> str: ...

class MethodReturnValue(CoerciveIntFlag):
    PAGE_BREAK: int
    LINES: int
    HEIGHT: int

class TableBordersLayout(CoerciveEnum):
    ALL: str
    NONE: str
    INTERNAL: str
    MINIMAL: str
    HORIZONTAL_LINES: str
    NO_HORIZONTAL_LINES: str
    SINGLE_TOP_LINE: str

class TableCellFillMode(CoerciveEnum):
    NONE: str
    ALL: str
    ROWS: str
    COLUMNS: str

    def should_fill_cell(self, i: int, j: int) -> bool: ...

class TableSpan(CoerciveEnum):
    ROW: Literal["ROW"]
    COL: Literal["COL"]

class RenderStyle(CoerciveEnum):
    D: str
    F: str
    DF: str
    @property
    def operator(self) -> str: ...
    @property
    def is_draw(self) -> bool: ...
    @property
    def is_fill(self) -> bool: ...

class TextMode(CoerciveIntEnum):
    FILL: int
    STROKE: int
    FILL_STROKE: int
    INVISIBLE: int
    FILL_CLIP: int
    STROKE_CLIP: int
    FILL_STROKE_CLIP: int
    CLIP: int

class XPos(CoerciveEnum):
    LEFT: str
    RIGHT: str
    START: str
    END: str
    WCONT: str
    CENTER: str
    LMARGIN: str
    RMARGIN: str

class YPos(CoerciveEnum):
    TOP: str
    LAST: str
    NEXT: str
    TMARGIN: str
    BMARGIN: str

class Angle(CoerciveIntEnum):
    NORTH: int
    EAST: int
    SOUTH: int
    WEST: int
    NORTHEAST: int
    SOUTHEAST: int
    SOUTHWEST: int
    NORTHWEST: int

class PageLayout(CoerciveEnum):
    SINGLE_PAGE: Name
    ONE_COLUMN: Name
    TWO_COLUMN_LEFT: Name
    TWO_COLUMN_RIGHT: Name
    TWO_PAGE_LEFT: Name
    TWO_PAGE_RIGHT: Name

class PageMode(CoerciveEnum):
    USE_NONE: Name
    USE_OUTLINES: Name
    USE_THUMBS: Name
    FULL_SCREEN: Name
    USE_OC: Name
    USE_ATTACHMENTS: Name

class TextMarkupType(CoerciveEnum):
    HIGHLIGHT: Name
    UNDERLINE: Name
    SQUIGGLY: Name
    STRIKE_OUT: Name

class BlendMode(CoerciveEnum):
    NORMAL: Name
    MULTIPLY: Name
    SCREEN: Name
    OVERLAY: Name
    DARKEN: Name
    LIGHTEN: Name
    COLOR_DODGE: Name
    COLOR_BURN: Name
    HARD_LIGHT: Name
    SOFT_LIGHT: Name
    DIFFERENCE: Name
    EXCLUSION: Name
    HUE: Name
    SATURATION: Name
    COLOR: Name
    LUMINOSITY: Name

class AnnotationFlag(CoerciveIntEnum):
    INVISIBLE: int
    HIDDEN: int
    PRINT: int
    NO_ZOOM: int
    NO_ROTATE: int
    NO_VIEW: int
    READ_ONLY: int
    LOCKED: int
    TOGGLE_NO_VIEW: int
    LOCKED_CONTENTS: int

class AnnotationName(CoerciveEnum):
    NOTE: Name
    COMMENT: Name
    HELP: Name
    PARAGRAPH: Name
    NEW_PARAGRAPH: Name
    INSERT: Name

class FileAttachmentAnnotationName(CoerciveEnum):
    PUSH_PIN: Name
    GRAPH_PUSH_PIN: Name
    PAPERCLIP_TAG: Name

class IntersectionRule(CoerciveEnum):
    NONZERO: str
    EVENODD: str

class PathPaintRule(CoerciveEnum):
    STROKE: str
    FILL_NONZERO: str
    FILL_EVENODD: str
    STROKE_FILL_NONZERO: str
    STROKE_FILL_EVENODD: str
    DONT_PAINT: str
    AUTO: str

class ClippingPathIntersectionRule(CoerciveEnum):
    NONZERO: str
    EVENODD: str

class StrokeCapStyle(CoerciveIntEnum):
    BUTT: int
    ROUND: int
    SQUARE: int

class StrokeJoinStyle(CoerciveIntEnum):
    MITER: int
    ROUND: int
    BEVEL: int

class PDFStyleKeys(Enum):
    FILL_ALPHA: Name
    BLEND_MODE: Name
    STROKE_ALPHA: Name
    STROKE_ADJUSTMENT: Name
    STROKE_WIDTH: Name
    STROKE_CAP_STYLE: Name
    STROKE_JOIN_STYLE: Name
    STROKE_MITER_LIMIT: Name
    STROKE_DASH_PATTERN: Name

class Corner(CoerciveEnum):
    TOP_RIGHT: str
    TOP_LEFT: str
    BOTTOM_RIGHT: str
    BOTTOM_LEFT: str

class FontDescriptorFlags(Flag):
    FIXED_PITCH: int
    SYMBOLIC: int
    ITALIC: int
    FORCE_BOLD: int

class AccessPermission(IntFlag):
    PRINT_LOW_RES: int
    MODIFY: int
    COPY: int
    ANNOTATION: int
    FILL_FORMS: int
    COPY_FOR_ACCESSIBILITY: int
    ASSEMBLE: int
    PRINT_HIGH_RES: int
    @classmethod
    def all(cls) -> int: ...
    @classmethod
    def none(cls) -> Literal[0]: ...

class EncryptionMethod(Enum):
    NO_ENCRYPTION: int
    RC4: int
    AES_128: int
    AES_256: int
