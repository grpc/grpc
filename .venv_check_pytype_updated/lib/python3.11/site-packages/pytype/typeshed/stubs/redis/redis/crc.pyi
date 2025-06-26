from redis.typing import EncodedT

REDIS_CLUSTER_HASH_SLOTS: int

def key_slot(key: EncodedT, bucket: int = 16384) -> int: ...
