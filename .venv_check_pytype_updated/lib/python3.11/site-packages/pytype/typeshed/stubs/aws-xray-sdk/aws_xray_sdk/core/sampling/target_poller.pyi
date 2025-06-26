from typing import Any

log: Any

class TargetPoller:
    def __init__(self, cache, rule_poller, connector) -> None: ...
    def start(self) -> None: ...
