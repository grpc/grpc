# TODO: Flowerbox.

import threading

import grpc
from typing import Any, Callable, Optional, Sequence, Text, Tuple, Union

_CHANNEL_CACHE = None
_CHANNEL_CACHE_LOCK = threading.RLock()

# TODO: Evict channels.

# Eviction policy based on both channel count and time since use. Perhaps
# OrderedDict instead?

def _create_channel(target: Text,
                    options: Sequence[Tuple[Text, Text]],
                    channel_credentials: Optional[grpc.ChannelCredentials],
                    compression: Optional[grpc.Compression]) -> grpc.Channel:
    if channel_credentials is None:
        return grpc.insecure_channel(target,
                                     options=options,
                                     compression=compression)
    else:
        return grpc.secure_channel(target,
                                   credentials=channel_credentials,
                                   options=options,
                                   compression=compression)


def _get_cached_channel(target: Text,
                        options: Sequence[Tuple[Text, Text]],
                        channel_credentials: Optional[grpc.ChannelCredentials],
                        compression: Optional[grpc.Compression]) -> grpc.Channel:
    global _CHANNEL_CACHE
    global _CHANNEL_CACHE_LOCK
    key = (target, options, channel_credentials, compression)
    with _CHANNEL_CACHE_LOCK:
        if _CHANNEL_CACHE is None:
            _CHANNEL_CACHE = {}
        channel = _CHANNEL_CACHE.get(key, None)
        if channel is not None:
            return channel
        else:
            channel = _create_channel(target, options, channel_credentials, compression)
            _CHANNEL_CACHE[key] = channel
            return channel


def unary_unary(request: Any,
                target: Text,
                method: Text,
                request_serializer: Optional[Callable[[Any], bytes]] = None,
                request_deserializer: Optional[Callable[[bytes], Any]] = None,
                options: Sequence[Tuple[Text, Text]] = (),
                # TODO: Somehow make insecure_channel opt-in, not the default.
                channel_credentials: Optional[grpc.ChannelCredentials] = None,
                call_credentials: Optional[grpc.CallCredentials] = None,
                compression: Optional[grpc.Compression] = None,
                wait_for_ready: Optional[bool] = None,
                metadata: Optional[Sequence[Tuple[Text, Union[Text, bytes]]]] = None) -> Any:
    """Invokes a unary RPC without an explicitly specified channel.

    This is backed by an LRU cache of channels evicted by a background thread
    on a periodic basis.

    TODO: Document the parameters and return value.
    """
    channel = _get_cached_channel(target, options, channel_credentials, compression)
    multicallable = channel.unary_unary(method, request_serializer, request_deserializer)
    return multicallable(request,
                         metadata=metadata,
                         wait_for_ready=wait_for_ready,
                         credentials=call_credentials)
