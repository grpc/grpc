from _typeshed import Incomplete
from typing import Any

# TODO: define and use:
# from redis.asyncio.cluster import ClusterNode

class CommandsParser:
    async def initialize(self, node: Incomplete | None = None) -> None: ...  # TODO: ClusterNode
    async def get_keys(self, *args: Any) -> tuple[str, ...] | None: ...
