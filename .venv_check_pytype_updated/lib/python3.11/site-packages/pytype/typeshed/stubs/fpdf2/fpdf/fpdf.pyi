import datetime
from _typeshed import Incomplete, StrPath, Unused
from collections.abc import Callable, Generator, Iterable, Sequence
from contextlib import _GeneratorContextManager
from io import BytesIO
from pathlib import PurePath
from re import Pattern
from typing import Any, ClassVar, Final, Literal, NamedTuple, overload
from typing_extensions import TypeAlias, deprecated

from fpdf import ViewerPreferences
from PIL import Image

from .annotations import AnnotationDict, PDFEmbeddedFile
from .drawing import DeviceGray, DeviceRGB, DrawingContext, PaintedPath
from .enums import (
    Align,
    AnnotationFlag,
    AnnotationName,
    Corner,
    FileAttachmentAnnotationName,
    MethodReturnValue,
    PageLayout,
    PathPaintRule,
    RenderStyle,
    TableBordersLayout,
    TableCellFillMode,
    TextMarkupType,
    TextMode as TextMode,
    WrapMode as WrapMode,
    XPos as XPos,
    YPos as YPos,
)
from .errors import FPDFException as FPDFException
from .fonts import FontFace
from .graphics_state import GraphicsStateMixin
from .html import HTML2FPDF
from .image_datastructures import (
    ImageCache,
    ImageInfo as ImageInfo,
    RasterImageInfo as RasterImageInfo,
    VectorImageInfo as VectorImageInfo,
    _AlignLiteral,
)
from .output import OutputProducer, PDFPage
from .recorder import FPDFRecorder
from .structure_tree import StructureTreeBuilder
from .syntax import DestinationXYZ
from .table import Table
from .util import _Unit

__all__ = [
    "FPDF",
    "XPos",
    "YPos",
    "get_page_format",
    "ImageInfo",
    "RasterImageInfo",
    "VectorImageInfo",
    "TextMode",
    "TitleStyle",
    "PAGE_FORMATS",
]

_Orientation: TypeAlias = Literal["", "portrait", "p", "P", "landscape", "l", "L"]
_Format: TypeAlias = Literal["", "a3", "A3", "a4", "A4", "a5", "A5", "letter", "Letter", "legal", "Legal"]
_FontStyle: TypeAlias = Literal["", "B", "I"]
_FontStyles: TypeAlias = Literal["", "B", "I", "U", "BU", "UB", "BI", "IB", "IU", "UI", "BIU", "BUI", "IBU", "IUB", "UBI", "UIB"]

FPDF_VERSION: Final[str]
PAGE_FORMATS: dict[_Format, tuple[float, float]]

class TitleStyle(FontFace):
    t_margin: int | None
    l_margin: int | None
    b_margin: int | None

    def __init__(
        self,
        font_family: str | None = None,
        font_style: str | None = None,
        font_size_pt: int | None = None,
        color: int | tuple[int, int, int] | None = None,
        underline: bool = False,
        t_margin: int | None = None,
        l_margin: int | None = None,
        b_margin: int | None = None,
    ) -> None: ...

class ToCPlaceholder(NamedTuple):
    render_function: Callable[[FPDF, Any], object]
    start_page: int
    y: int
    pages: int = ...

def get_page_format(format: _Format | tuple[float, float], k: float | None = None) -> tuple[float, float]: ...

# TODO: TypedDicts
_Font: TypeAlias = dict[str, Any]

class FPDF(GraphicsStateMixin):
    MARKDOWN_BOLD_MARKER: ClassVar[str]
    MARKDOWN_ITALICS_MARKER: ClassVar[str]
    MARKDOWN_UNDERLINE_MARKER: ClassVar[str]
    MARKDOWN_LINK_REGEX: ClassVar[Pattern[str]]
    MARKDOWN_LINK_COLOR: ClassVar[Incomplete | None]
    MARKDOWN_LINK_UNDERLINE: ClassVar[bool]

    HTML2FPDF_CLASS: ClassVar[type[HTML2FPDF]]

    page: int
    pages: dict[int, PDFPage]
    fonts: dict[str, _Font]
    links: dict[int, DestinationXYZ]
    embedded_files: list[PDFEmbeddedFile]
    image_cache: ImageCache

    in_footer: bool
    str_alias_nb_pages: str

    xmp_metadata: str | None
    page_duration: int
    page_transition: Incomplete | None
    allow_images_transparency: bool
    oversized_images: Incomplete | None
    oversized_images_ratio: float
    struct_builder: StructureTreeBuilder
    section_title_styles: dict[int, Incomplete]

    core_fonts: dict[str, str]
    core_fonts_encoding: str
    font_aliases: dict[str, str]
    k: float

    page_background: Incomplete | None

    dw_pt: float
    dh_pt: float
    def_orientation: Literal["P", "L"]
    x: float
    y: float
    l_margin: float
    t_margin: float
    c_margin: float
    viewer_preferences: ViewerPreferences | None
    compress: bool
    pdf_version: str
    creation_date: datetime.datetime

    buffer: bytearray | None

    # Set during call to _set_orientation(), called from __init__().
    cur_orientation: Literal["P", "L"]
    w_pt: float
    h_pt: float
    w: float
    h: float

    def __init__(
        self,
        orientation: _Orientation = "portrait",
        unit: _Unit | float = "mm",
        format: _Format | tuple[float, float] = "A4",
        font_cache_dir: Literal["DEPRECATED"] = "DEPRECATED",
    ) -> None: ...
    # The following definition crashes stubtest 1.1.1.
    # def set_encryption(
    #     self,
    #     owner_password: str,
    #     user_password: str | None = None,
    #     encryption_method: EncryptionMethod | str = ...,
    #     permissions: AccessPermission = ...,
    #     encrypt_metadata: bool = False,
    # ) -> None: ...
    # args and kwargs are passed to HTML2FPDF_CLASS constructor.
    def write_html(self, text: str, *args: Any, **kwargs: Any) -> None: ...
    @property
    def is_ttf_font(self) -> bool: ...
    @property
    def page_mode(self): ...
    @property
    def epw(self) -> float: ...
    @property
    def eph(self) -> float: ...
    @property
    def pages_count(self) -> int: ...
    def set_margin(self, margin: float) -> None: ...
    def set_margins(self, left: float, top: float, right: float = -1) -> None: ...
    def set_left_margin(self, margin: float) -> None: ...
    def set_top_margin(self, margin: float) -> None: ...
    r_margin: float
    def set_right_margin(self, margin: float) -> None: ...
    auto_page_break: bool
    b_margin: float
    page_break_trigger: float
    def set_auto_page_break(self, auto: bool, margin: float = 0) -> None: ...
    @property
    def default_page_dimensions(self) -> tuple[float, float]: ...
    zoom_mode: Literal["fullpage", "fullwidth", "real", "default"] | float
    page_layout: PageLayout | None
    def set_display_mode(
        self,
        zoom: Literal["fullpage", "fullwidth", "real", "default"] | float,
        layout: Literal["single", "continuous", "two", "default"] = "continuous",
    ) -> None: ...
    def set_text_shaping(
        self,
        use_shaping_engine: bool = True,
        features: dict[str, bool] | None = None,
        direction: Literal["ltr", "rtl"] | None = None,
        script: str | None = None,
        language: str | None = None,
    ) -> None: ...
    def set_compression(self, compress: bool) -> None: ...
    title: str
    def set_title(self, title: str) -> None: ...
    lang: str
    def set_lang(self, lang: str) -> None: ...
    subject: str
    def set_subject(self, subject: str) -> None: ...
    author: str
    def set_author(self, author: str) -> None: ...
    keywords: str
    def set_keywords(self, keywords: str) -> None: ...
    creator: str
    def set_creator(self, creator: str) -> None: ...
    producer: str
    def set_producer(self, producer: str) -> None: ...
    def set_creation_date(self, date: datetime.datetime) -> None: ...
    def set_xmp_metadata(self, xmp_metadata: str) -> None: ...
    def set_doc_option(self, opt: str, value: str) -> None: ...
    def set_image_filter(self, image_filter: str) -> None: ...
    def alias_nb_pages(self, alias: str = "{nb}") -> None: ...
    def add_page(
        self,
        orientation: _Orientation = "",
        format: _Format | tuple[float, float] = "",
        same: bool = False,
        duration: int = 0,
        transition: Incomplete | None = None,
    ) -> None: ...
    def header(self) -> None: ...
    def footer(self) -> None: ...
    def page_no(self) -> int: ...
    def set_draw_color(self, r: int, g: int = -1, b: int = -1) -> None: ...
    def set_fill_color(self, r: int, g: int = -1, b: int = -1) -> None: ...
    def set_text_color(self, r: int, g: int = -1, b: int = -1) -> None: ...
    def get_string_width(self, s: str, normalized: bool = False, markdown: bool = False) -> float: ...
    def set_line_width(self, width: float) -> None: ...
    def set_page_background(self, background) -> None: ...
    def drawing_context(self, debug_stream: Incomplete | None = None) -> _GeneratorContextManager[DrawingContext]: ...
    def new_path(
        self, x: float = 0, y: float = 0, paint_rule: PathPaintRule = ..., debug_stream: Incomplete | None = None
    ) -> _GeneratorContextManager[PaintedPath]: ...
    def draw_path(self, path: PaintedPath, debug_stream: Incomplete | None = None) -> None: ...
    def set_dash_pattern(self, dash: float = 0, gap: float = 0, phase: float = 0) -> None: ...
    def line(self, x1: float, y1: float, x2: float, y2: float) -> None: ...
    def polyline(
        self,
        point_list: list[tuple[float, float]],
        fill: bool = False,
        polygon: bool = False,
        style: RenderStyle | str | None = None,
    ) -> None: ...
    def polygon(
        self, point_list: list[tuple[float, float]], fill: bool = False, style: RenderStyle | str | None = None
    ) -> None: ...
    def dashed_line(self, x1, y1, x2, y2, dash_length: int = 1, space_length: int = 1) -> None: ...
    def rect(
        self,
        x: float,
        y: float,
        w: float,
        h: float,
        style: RenderStyle | str | None = None,
        round_corners: tuple[str, ...] | tuple[Corner, ...] | bool = False,
        corner_radius: float = 0,
    ) -> None: ...
    def ellipse(self, x: float, y: float, w: float, h: float, style: RenderStyle | str | None = None) -> None: ...
    def circle(self, x: float, y: float, r, style: RenderStyle | str | None = None) -> None: ...
    def regular_polygon(
        self,
        x: float,
        y: float,
        numSides: int,
        polyWidth: float,
        rotateDegrees: float = 0,
        style: RenderStyle | str | None = None,
    ): ...
    def star(
        self,
        x: float,
        y: float,
        r_in: float,
        r_out: float,
        corners: int,
        rotate_degrees: float = 0,
        style: RenderStyle | str | None = None,
    ): ...
    def arc(
        self,
        x: float,
        y: float,
        a: float,
        start_angle: float,
        end_angle: float,
        b: float | None = None,
        inclination: float = 0,
        clockwise: bool = False,
        start_from_center: bool = False,
        end_at_center: bool = False,
        style: RenderStyle | str | None = None,
    ) -> None: ...
    def solid_arc(
        self,
        x: float,
        y: float,
        a: float,
        start_angle: float,
        end_angle: float,
        b: float | None = None,
        inclination: float = 0,
        clockwise: bool = False,
        style: RenderStyle | str | None = None,
    ) -> None: ...
    def add_font(
        self,
        family: str | None = None,
        style: _FontStyle = "",
        fname: str | PurePath | None = None,
        uni: bool | Literal["DEPRECATED"] = "DEPRECATED",
    ) -> None: ...
    def set_font(self, family: str | None = None, style: _FontStyles = "", size: int = 0) -> None: ...
    def set_font_size(self, size: float) -> None: ...
    def set_char_spacing(self, spacing: float) -> None: ...
    def set_stretching(self, stretching: float) -> None: ...
    def set_fallback_fonts(self, fallback_fonts: Iterable[str], exact_match: bool = True) -> None: ...
    def add_link(self, y: float = 0, x: float = 0, page: int = -1, zoom: float | Literal["null"] = "null") -> int: ...
    def set_link(self, link, y: float = 0, x: float = 0, page: int = -1, zoom: float | Literal["null"] = "null") -> None: ...
    def link(
        self, x: float, y: float, w: float, h: float, link: str | int, alt_text: str | None = None, border_width: int = 0
    ) -> AnnotationDict: ...
    def embed_file(
        self,
        file_path: StrPath | None = None,
        bytes: bytes | None = None,
        basename: str | None = None,
        modification_date: datetime.datetime | None = None,
        *,
        creation_date: datetime.datetime | None = ...,
        desc: str = ...,
        compress: bool = ...,
        checksum: bool = ...,
    ) -> str: ...
    def file_attachment_annotation(
        self,
        file_path: StrPath,
        x: float,
        y: float,
        w: float = 1,
        h: float = 1,
        name: FileAttachmentAnnotationName | str | None = None,
        flags: Iterable[AnnotationFlag | str] = ...,
        *,
        bytes: bytes | None = ...,
        basename: str | None = ...,
        creation_date: datetime.datetime | None = ...,
        modification_date: datetime.datetime | None = ...,
        desc: str = ...,
        compress: bool = ...,
        checksum: bool = ...,
    ) -> AnnotationDict: ...
    def text_annotation(
        self,
        x: float,
        y: float,
        text: str,
        w: float = 1,
        h: float = 1,
        name: AnnotationName | str | None = None,
        flags: tuple[AnnotationFlag, ...] | tuple[str, ...] = ...,
    ) -> AnnotationDict: ...
    def free_text_annotation(
        self,
        text: str,
        x: float | None = None,
        y: float | None = None,
        w: float | None = None,
        h: float | None = None,
        flags: tuple[AnnotationFlag, ...] | tuple[str, ...] = ...,
    ) -> AnnotationDict: ...
    def add_action(self, action, x: float, y: float, w: float, h: float) -> None: ...
    def highlight(
        self,
        text: str,
        title: str = "",
        type: TextMarkupType | str = "Highlight",
        color: tuple[float, float, float] = (1, 1, 0),
        modification_time: datetime.datetime | None = None,
    ) -> _GeneratorContextManager[None]: ...
    add_highlight = highlight
    def add_text_markup_annotation(
        self,
        type: str,
        text: str,
        quad_points: Sequence[int],
        title: str = "",
        color: tuple[float, float, float] = (1, 1, 0),
        modification_time: datetime.datetime | None = None,
        page: int | None = None,
    ) -> AnnotationDict: ...
    def ink_annotation(
        self,
        coords: Iterable[Incomplete],
        contents: str = "",
        title: str = "",
        color: Sequence[float] = (1, 1, 0),
        border_width: int = 1,
    ) -> AnnotationDict: ...
    def text(self, x: float, y: float, text: str = "") -> None: ...
    def rotate(self, angle: float, x: float | None = None, y: float | None = None) -> None: ...
    def rotation(self, angle: float, x: float | None = None, y: float | None = None) -> _GeneratorContextManager[None]: ...
    def skew(
        self, ax: float = 0, ay: float = 0, x: float | None = None, y: float | None = None
    ) -> _GeneratorContextManager[None]: ...
    def mirror(self, origin, angle) -> Generator[None, None, None]: ...
    def local_context(
        self,
        font_family: Incomplete | None = None,
        font_style: Incomplete | None = None,
        font_size: Incomplete | None = None,
        line_width: Incomplete | None = None,
        draw_color: Incomplete | None = None,
        fill_color: Incomplete | None = None,
        text_color: Incomplete | None = None,
        dash_pattern: Incomplete | None = None,
        **kwargs,
    ) -> _GeneratorContextManager[None]: ...
    @property
    def accept_page_break(self) -> bool: ...
    def cell(
        self,
        w: float | None = None,
        h: float | None = None,
        text: str = "",
        border: bool | Literal[0, 1] | str = 0,
        ln: int | Literal["DEPRECATED"] = "DEPRECATED",
        align: str | Align = ...,
        fill: bool = False,
        link: str = "",
        center: bool = False,
        markdown: bool = False,
        new_x: XPos | str = ...,
        new_y: YPos | str = ...,
    ) -> bool: ...
    def get_fallback_font(self, char: str, style: str = "") -> str | None: ...
    def will_page_break(self, height: float) -> bool: ...
    def multi_cell(
        self,
        w: float,
        h: float | None = None,
        text: str = "",
        border: bool | Literal[0, 1] | str = 0,
        align: str | Align = ...,
        fill: bool = False,
        split_only: bool = False,
        link: str = "",
        ln: int | Literal["DEPRECATED"] = "DEPRECATED",
        max_line_height: float | None = None,
        markdown: bool = False,
        print_sh: bool = False,
        new_x: XPos | str = ...,
        new_y: YPos | str = ...,
        wrapmode: WrapMode = ...,
        dry_run: bool = False,
        output: MethodReturnValue | str | int = ...,
        center: bool = False,
        padding: int = 0,
    ): ...
    def write(
        self, h: float | None = None, text: str = "", link: str = "", print_sh: bool = False, wrapmode: WrapMode = ...
    ) -> bool: ...
    def text_columns(
        self,
        text: str | None = None,
        img: str | None = None,
        img_fill_width: bool = False,
        ncols: int = 1,
        gutter: float = 10,
        balance: bool = False,
        text_align: Align | _AlignLiteral = "LEFT",
        line_height: float = 1,
        l_margin: float | None = None,
        r_margin: float | None = None,
        print_sh: bool = False,
        wrapmode: WrapMode = ...,
        skip_leading_spaces: bool = False,
    ): ...
    def image(
        self,
        name: str | Image.Image | BytesIO | StrPath,
        x: float | Align | None = None,
        y: float | None = None,
        w: float = 0,
        h: float = 0,
        type: str = "",
        link: str = "",
        title: str | None = None,
        alt_text: str | None = None,
        dims: tuple[float, float] | None = None,
        keep_aspect_ratio: bool = False,
    ) -> RasterImageInfo | VectorImageInfo: ...
    @deprecated("Deprecated since 2.7.7; use fpdf.image_parsing.preload_image() instead")
    def preload_image(
        self, name: str | Image.Image | BytesIO, dims: tuple[float, float] | None = None
    ) -> tuple[str, Any, ImageInfo]: ...
    def ln(self, h: float | None = None) -> None: ...
    def get_x(self) -> float: ...
    def set_x(self, x: float) -> None: ...
    def get_y(self) -> float: ...
    def set_y(self, y: float) -> None: ...
    def set_xy(self, x: float, y: float) -> None: ...
    def normalize_text(self, text: str) -> str: ...
    def sign_pkcs12(
        self,
        pkcs_filepath: str,
        password: bytes | None = None,
        hashalgo: str = "sha256",
        contact_info: str | None = None,
        location: str | None = None,
        signing_time: datetime.datetime | None = None,
        reason: str | None = None,
        flags: tuple[AnnotationFlag, ...] = ...,
    ) -> None: ...
    def sign(
        self,
        key,
        cert,
        extra_certs: Sequence[Incomplete] = (),
        hashalgo: str = "sha256",
        contact_info: str | None = None,
        location: str | None = None,
        signing_time: datetime.datetime | None = None,
        reason: str | None = None,
        flags: tuple[AnnotationFlag, ...] = ...,
    ) -> None: ...
    def file_id(self) -> str: ...
    def interleaved2of5(self, text, x: float, y: float, w: float = 1, h: float = 10) -> None: ...
    def code39(self, text, x: float, y: float, w: float = 1.5, h: float = 5) -> None: ...
    def rect_clip(self, x: float, y: float, w: float, h: float) -> _GeneratorContextManager[None]: ...
    def elliptic_clip(self, x: float, y: float, w: float, h: float) -> _GeneratorContextManager[None]: ...
    def round_clip(self, x: float, y: float, r: float) -> _GeneratorContextManager[None]: ...
    def unbreakable(self) -> _GeneratorContextManager[FPDFRecorder]: ...
    def offset_rendering(self) -> _GeneratorContextManager[FPDFRecorder]: ...
    def insert_toc_placeholder(self, render_toc_function, pages: int = 1) -> None: ...
    def set_section_title_styles(
        self,
        level0: TitleStyle,
        level1: TitleStyle | None = None,
        level2: TitleStyle | None = None,
        level3: TitleStyle | None = None,
        level4: TitleStyle | None = None,
        level5: TitleStyle | None = None,
        level6: TitleStyle | None = None,
    ) -> None: ...
    def start_section(self, name: str, level: int = 0, strict: bool = True) -> None: ...
    def use_font_face(self, font_face: FontFace) -> _GeneratorContextManager[None]: ...
    def table(
        self,
        rows: Iterable[Incomplete] = (),
        *,
        align: str | Align = "CENTER",
        borders_layout: str | TableBordersLayout = ...,
        cell_fill_color: int | tuple[Incomplete, ...] | DeviceGray | DeviceRGB | None = None,
        cell_fill_mode: str | TableCellFillMode = ...,
        col_widths: int | tuple[int, ...] | None = None,
        first_row_as_headings: bool = True,
        headings_style: FontFace = ...,
        line_height: int | None = None,
        markdown: bool = False,
        text_align: str | Align = "JUSTIFY",
        width: int | None = None,
    ) -> _GeneratorContextManager[Table]: ...
    @overload
    def output(  # type: ignore[overload-overlap]
        self,
        name: Literal[""] | None = "",
        dest: Unused = "",
        linearize: bool = False,
        output_producer_class: Callable[[FPDF], OutputProducer] = ...,
    ) -> bytearray: ...
    @overload
    def output(
        self, name: str, dest: Unused = "", linearize: bool = False, output_producer_class: Callable[[FPDF], OutputProducer] = ...
    ) -> None: ...
