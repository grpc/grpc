from typing import ClassVar

import passlib.utils.handlers as uh

class postgres_md5(uh.HasUserContext, uh.StaticHandler):
    name: ClassVar[str]
    checksum_chars: ClassVar[str]
    checksum_size: ClassVar[int]
