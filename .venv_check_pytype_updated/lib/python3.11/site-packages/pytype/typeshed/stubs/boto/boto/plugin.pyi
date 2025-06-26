from _typeshed import Incomplete
from typing import Any

class Plugin:
    capability: Any
    @classmethod
    def is_capable(cls, requested_capability): ...

def get_plugin(cls, requested_capability: Incomplete | None = None): ...
def load_plugins(config): ...
