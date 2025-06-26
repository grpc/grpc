from datetime import datetime
from typing import Any

TIME_MATCHER: Any
MS_SEARCHER: Any

def patch_strptime() -> Any: ...
def strptime(date_string, format) -> datetime: ...
