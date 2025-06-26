from typing import Any

from .commands import *
from .info import BFInfo as BFInfo, CFInfo as CFInfo, CMSInfo as CMSInfo, TDigestInfo as TDigestInfo, TopKInfo as TopKInfo

class AbstractBloom:
    @staticmethod
    def append_items(params, items) -> None: ...
    @staticmethod
    def append_error(params, error) -> None: ...
    @staticmethod
    def append_capacity(params, capacity) -> None: ...
    @staticmethod
    def append_expansion(params, expansion) -> None: ...
    @staticmethod
    def append_no_scale(params, noScale) -> None: ...
    @staticmethod
    def append_weights(params, weights) -> None: ...
    @staticmethod
    def append_no_create(params, noCreate) -> None: ...
    @staticmethod
    def append_items_and_increments(params, items, increments) -> None: ...
    @staticmethod
    def append_values_and_weights(params, items, weights) -> None: ...
    @staticmethod
    def append_max_iterations(params, max_iterations) -> None: ...
    @staticmethod
    def append_bucket_size(params, bucket_size) -> None: ...

class CMSBloom(CMSCommands, AbstractBloom):
    client: Any
    commandmixin: Any
    execute_command: Any
    def __init__(self, client, **kwargs) -> None: ...

class TOPKBloom(TOPKCommands, AbstractBloom):
    client: Any
    commandmixin: Any
    execute_command: Any
    def __init__(self, client, **kwargs) -> None: ...

class CFBloom(CFCommands, AbstractBloom):
    client: Any
    commandmixin: Any
    execute_command: Any
    def __init__(self, client, **kwargs) -> None: ...

class TDigestBloom(TDigestCommands, AbstractBloom):
    client: Any
    commandmixin: Any
    execute_command: Any
    def __init__(self, client, **kwargs) -> None: ...

class BFBloom(BFCommands, AbstractBloom):
    client: Any
    commandmixin: Any
    execute_command: Any
    def __init__(self, client, **kwargs) -> None: ...
