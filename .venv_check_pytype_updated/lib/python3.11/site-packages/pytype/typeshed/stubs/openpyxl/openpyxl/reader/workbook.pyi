from _typeshed import Incomplete
from collections.abc import Generator
from zipfile import ZipFile

from openpyxl.packaging.relationship import Relationship, RelationshipList
from openpyxl.packaging.workbook import PivotCache
from openpyxl.pivot.cache import CacheDefinition
from openpyxl.workbook import Workbook

class WorkbookParser:
    archive: ZipFile
    workbook_part_name: str
    wb: Workbook
    keep_links: bool
    sheets: list[Incomplete]
    def __init__(self, archive: ZipFile, workbook_part_name: str, keep_links: bool = True) -> None: ...
    @property
    def rels(self) -> RelationshipList: ...
    # Errors if "parse" is never called.
    caches: list[PivotCache]
    def parse(self) -> None: ...
    def find_sheets(self) -> Generator[tuple[Incomplete, Relationship], None, None]: ...
    def assign_names(self) -> None: ...
    @property
    def pivot_caches(self) -> dict[int, CacheDefinition]: ...
