# TODO: Flowerbox.

import collections
import datetime
import os
import logging
import threading

import grpc
from typing import Any, Callable, Optional, Sequence, Text, Tuple, Union


_LOGGER = logging.getLogger(__name__)

_EVICTION_PERIOD_KEY = "GRPC_PYTHON_MANAGED_CHANNEL_EVICTION_SECONDS"
if _EVICTION_PERIOD_KEY in os.environ:
    _EVICTION_PERIOD = datetime.timedelta(seconds=float(os.environ[_EVICTION_PERIOD_KEY]))
else:
    _EVICTION_PERIOD = datetime.timedelta(minutes=10)


def _create_channel(target: Text,
                    options: Sequence[Tuple[Text, Text]],
                    channel_credentials: Optional[grpc.ChannelCredentials],
                    compression: Optional[grpc.Compression]) -> grpc.Channel:
    if channel_credentials is None:
        _LOGGER.info(f"Creating insecure channel with options '{options}' " +
                       f"and compression '{compression}'")
        return grpc.insecure_channel(target,
                                     options=options,
                                     compression=compression)
    else:
        _LOGGER.info(f"Creating secure channel with credentials '{channel_credentials}', " +
                       f"options '{options}' and compression '{compression}'")
        return grpc.secure_channel(target,
                                   credentials=channel_credentials,
                                   options=options,
                                   compression=compression)

class ChannelCache:
    _singleton = None
    _lock = threading.RLock()
    _condition = threading.Condition(lock=_lock)
    _eviction_ready = threading.Event()


    def __init__(self):
        self._mapping = collections.OrderedDict()
        self._eviction_thread = threading.Thread(target=ChannelCache._perform_evictions, daemon=True)
        self._eviction_thread.start()


    @staticmethod
    def get():
        with ChannelCache._lock:
            if ChannelCache._singleton is None:
                ChannelCache._singleton = ChannelCache()
        ChannelCache._eviction_ready.wait()
        return ChannelCache._singleton

    # TODO: Type annotate key.
    def _evict_locked(self, key):
        channel, _ = self._mapping.pop(key)
        _LOGGER.info(f"Evicting channel {channel} with configuration {key}.")
        channel.close()
        del channel


    # TODO: Refactor. Way too deeply nested.
    @staticmethod
    def _perform_evictions():
        while True:
            with ChannelCache._lock:
                ChannelCache._eviction_ready.set()
                if not ChannelCache._singleton._mapping:
                    ChannelCache._condition.wait()
                else:
                    key, (channel, eviction_time) = next(iter(ChannelCache._singleton._mapping.items()))
                    now = datetime.datetime.now()
                    if eviction_time <= now:
                        ChannelCache._singleton._evict_locked(key)
                        continue
                    else:
                        time_to_eviction = (eviction_time - now).total_seconds()
                        ChannelCache._condition.wait(timeout=time_to_eviction)



    def get_channel(self,
                    target: Text,
                    options: Sequence[Tuple[Text, Text]],
                    channel_credentials: Optional[grpc.ChannelCredentials],
                    compression: Optional[grpc.Compression]) -> grpc.Channel:
        key = (target, options, channel_credentials, compression)
        with self._lock:
            # TODO: Can the get and the pop be turned into a single operation?
            channel_data = self._mapping.get(key, None)
            if channel_data is not None:
                channel = channel_data[0]
                # # NOTE: This isn't actually necessary. The eviction thread will
                # # always wake up because the new head of the list will, by
                # # definition, have a later eviction time than the old head of
                # # the list. If however, we allow for channels with heterogeneous
                # # eviction periods, this *will* become necessary. We can imagine
                # # this would be the case for timeouts. That is, if a timeout
                # # longer than the eviction period is specified, we do not want
                # # to cancel the RPC prematurely.
                # if channel is next(iter(self._mapping.values()))[0]:
                #     self._condition.notify()
                # Move to the end of the map.
                self._mapping.pop(key)
                self._mapping[key] = (channel, datetime.datetime.now() + _EVICTION_PERIOD)
                return channel
            else:
                channel = _create_channel(target, options, channel_credentials, compression)
                self._mapping[key] = (channel, datetime.datetime.now() + _EVICTION_PERIOD)
                if len(self._mapping) == 1:
                    self._condition.notify()
                return channel

    def _test_only_channel_count(self) -> int:
        with self._lock:
            return len(self._mapping)


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
                timeout: Optional[float] = None,
                metadata: Optional[Sequence[Tuple[Text, Union[Text, bytes]]]] = None) -> Any:
    """Invokes a unary RPC without an explicitly specified channel.

    This is backed by a cache of channels evicted by a background thread
    on a periodic basis.

    TODO: Document the parameters and return value.
    """

    # TODO: Warn if the timeout is greater than the channel eviction time.
    channel = ChannelCache.get().get_channel(target, options, channel_credentials, compression)
    multicallable = channel.unary_unary(method, request_serializer, request_deserializer)
    return multicallable(request,
                         metadata=metadata,
                         wait_for_ready=wait_for_ready,
                         credentials=call_credentials,
                         timeout=timeout)
