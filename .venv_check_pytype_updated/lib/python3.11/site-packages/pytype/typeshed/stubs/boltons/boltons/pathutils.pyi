def augpath(
    path: str,
    suffix: str = "",
    prefix: str = "",
    ext: str | None = None,
    base: str | None = None,
    dpath: str | None = None,
    multidot: bool = False,
) -> str: ...
def shrinkuser(path: str, home: str = "~") -> str: ...
def expandpath(path: str) -> str: ...
