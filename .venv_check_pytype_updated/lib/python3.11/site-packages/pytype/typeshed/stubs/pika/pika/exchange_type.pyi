from enum import Enum

class ExchangeType(Enum):
    direct: str
    fanout: str
    headers: str
    topic: str
