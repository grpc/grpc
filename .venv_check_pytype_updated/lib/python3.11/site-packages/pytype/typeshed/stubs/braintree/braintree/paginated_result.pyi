from typing import Any

class PaginatedResult:
    total_items: Any
    page_size: Any
    current_page: Any
    def __init__(self, total_items, page_size, current_page) -> None: ...
