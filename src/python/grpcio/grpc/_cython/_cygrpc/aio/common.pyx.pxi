# Copyright 2019 The gRPC Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


cdef grpc_status_code get_status_code(object code) except *:
    if isinstance(code, int):
        if code >= StatusCode.ok and code <= StatusCode.data_loss:
            return code
        else:
            return StatusCode.unknown
    else:
        try:
            return code.value[0]
        except (KeyError, AttributeError):
            return StatusCode.unknown


cdef object deserialize(object deserializer, bytes raw_message):
    """Perform deserialization on raw bytes.

    Failure to deserialize is a fatal error.
    """
    if deserializer:
        return deserializer(raw_message)
    else:
        return raw_message


cdef bytes serialize(object serializer, object message):
    """Perform serialization on a message.

    Failure to serialize is a fatal error.
    """
    if isinstance(message, str):
        message = message.encode('utf-8')
    if serializer:
        return serializer(message)
    else:
        return message


class _EOF:

    def __bool__(self):
        return False
    
    def __len__(self):
        return 0

    def _repr(self) -> str:
        return '<grpc.aio.EOF>'

    def __repr__(self) -> str:
        return self._repr()

    def __str__(self) -> str:
        return self._repr()


EOF = _EOF()

_COMPRESSION_METADATA_STRING_MAPPING = {
    CompressionAlgorithm.none: 'identity',
    CompressionAlgorithm.deflate: 'deflate',
    CompressionAlgorithm.gzip: 'gzip',
}

class BaseError(Exception):
    """The base class for exceptions generated by gRPC AsyncIO stack."""


class UsageError(BaseError):
    """Raised when the usage of API by applications is inappropriate.

    For example, trying to invoke RPC on a closed channel, mixing two styles
    of streaming API on the client side. This exception should not be
    suppressed.
    """


class AbortError(BaseError):
    """Raised when calling abort in servicer methods.

    This exception should not be suppressed. Applications may catch it to
    perform certain clean-up logic, and then re-raise it.
    """


class InternalError(BaseError):
    """Raised upon unexpected errors in native code."""


def schedule_coro_threadsafe(object coro, object loop):
    try:
        return loop.create_task(coro)
    except RuntimeError as runtime_error:
        if 'Non-thread-safe operation' in str(runtime_error):
            return asyncio.run_coroutine_threadsafe(
                coro,
                loop,
            )
        else:
            raise


def async_generator_to_generator(object agen, object loop):
    """Converts an async generator into generator."""
    try:
        while True:
            future = asyncio.run_coroutine_threadsafe(
                agen.__anext__(),
                loop
            )
            response = future.result()
            if response is EOF:
                break
            else:
                yield response
    except StopAsyncIteration:
        # If StopAsyncIteration is raised, end this generator.
        pass


async def generator_to_async_generator(object gen, object loop, object thread_pool):
    """Converts a generator into async generator.

    The generator might block, so we need to delegate the iteration to thread
    pool. Also, we can't simply delegate __next__ to the thread pool, otherwise
    we will see following error:

        TypeError: StopIteration interacts badly with generators and cannot be
            raised into a Future
    """
    queue = asyncio.Queue(maxsize=1, loop=loop)

    def yield_to_queue():
        try:
            for item in gen:
                asyncio.run_coroutine_threadsafe(queue.put(item), loop).result()
        finally:
            asyncio.run_coroutine_threadsafe(queue.put(EOF), loop).result()

    future = loop.run_in_executor(
        thread_pool,
        yield_to_queue,
    )

    while True:
        response = await queue.get()
        if response is EOF:
            break
        else:
            yield response

    # Port the exception if there is any
    await future
