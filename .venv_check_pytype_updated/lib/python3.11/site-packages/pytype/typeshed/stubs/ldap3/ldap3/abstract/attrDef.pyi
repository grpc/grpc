from _typeshed import Incomplete
from typing import Any

class AttrDef:
    name: Any
    key: Any
    validate: Any
    pre_query: Any
    post_query: Any
    default: Any
    dereference_dn: Any
    description: Any
    mandatory: Any
    single_value: Any
    oid_info: Any
    other_names: Any
    def __init__(
        self,
        name,
        key: Incomplete | None = None,
        validate: Incomplete | None = None,
        pre_query: Incomplete | None = None,
        post_query: Incomplete | None = None,
        default=...,
        dereference_dn: Incomplete | None = None,
        description: Incomplete | None = None,
        mandatory: bool = False,
        single_value: Incomplete | None = None,
        alias: Incomplete | None = None,
    ) -> None: ...
    def __eq__(self, other): ...
    def __lt__(self, other): ...
    def __hash__(self) -> int: ...
    def __setattr__(self, key: str, value) -> None: ...
