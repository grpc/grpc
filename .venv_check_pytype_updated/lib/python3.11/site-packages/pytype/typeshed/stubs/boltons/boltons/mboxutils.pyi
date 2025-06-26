import mailbox

DEFAULT_MAXMEM: int

class mbox_readonlydir(mailbox.mbox):
    maxmem: int
    def __init__(self, path: str, factory: type | None = None, create: bool = True, maxmem: int = 1048576) -> None: ...
    def flush(self) -> None: ...
