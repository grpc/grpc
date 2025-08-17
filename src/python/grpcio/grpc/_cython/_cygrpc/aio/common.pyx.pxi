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

import warnings
import multiprocessing as mp
from multiprocessing import Pool
import time
import logging

from cpython.version cimport PY_MAJOR_VERSION, PY_MINOR_VERSION

TYPE_METADATA_STRING = "Tuple[Tuple[str, Union[str, bytes]]...]"


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


def deserialize(object deserializer, bytes raw_message, int chunk_size=1024*1024):
    """Perform deserialization on raw bytes using multiprocessing.

    Failure to deserialize is a fatal error.
    
    Args:
        deserializer: The deserializer function to use
        raw_message: The raw bytes message to deserialize
        chunk_size: Size of chunks for multiprocessing (in bytes)
    """
    return multiprocessing_deserialize_cy(raw_message, deserializer, chunk_size=chunk_size)


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


# ============================================================================
# Multiprocessing Chunked Deserialization Functions
# ============================================================================

def chunk_message_cy(bytes raw_message, int chunk_size):
    """
    Divide a large message into chunks for parallel processing.
    
    Args:
        raw_message: The raw bytes message to chunk
        chunk_size: Size of each chunk in bytes
    
    Returns:
        List of message chunks
    """
    cdef:
        int total_size = len(raw_message)
        int num_chunks = (total_size + chunk_size - 1) // chunk_size
        int i, start, end
        list chunks = []
    
    for i in range(num_chunks):
        start = i * chunk_size
        end = min(start + chunk_size, total_size)
        chunks.append(raw_message[start:end])
    
    return chunks


def deserialize_chunk_cy(bytes chunk, object deserializer=None):
    """
    Deserialize a single chunk of data.
    This function will run in a separate process.
    
    Args:
        chunk: The chunk of bytes to deserialize
        deserializer: The deserializer function to use (or None for raw bytes)
    
    Returns:
        Deserialized chunk data
    """
    try:
        if deserializer:
            # Use the provided deserializer function
            return deserializer(chunk)
        else:
            # Return raw bytes if no deserializer provided
            return chunk
    except Exception as e:
        logging.error(f"Error deserializing chunk: {e}")
        return None


def multiprocessing_deserialize_cy(bytes raw_message, object deserializer=None,
                                  int num_processes=0, int chunk_size=1024*1024):
    """
    Deserialize a message using multiprocessing and chunking.
    
    Args:
        raw_message: The raw bytes message to deserialize
        deserializer: The deserializer function to use (or None for raw bytes)
        num_processes: Number of processes to use (default: CPU count)
        chunk_size: Size of each chunk in bytes
    
    Returns:
        Deserialized message
    """
    if num_processes == 0:
        num_processes = mp.cpu_count()
    
    # Always use multiprocessing, even for small messages
    # Divide message into chunks
    chunks = chunk_message_cy(raw_message, chunk_size)
    
    # Process chunks in parallel
    with Pool(processes=num_processes) as pool:
        # Create arguments for each chunk
        chunk_args = [(chunk, deserializer) for chunk in chunks]
        
        # Process chunks in parallel
        results = pool.starmap(deserialize_chunk_cy, chunk_args)
    
    # Combine results from chunks
    return combine_chunk_results(results, deserializer)





def optimize_chunk_size(int message_size, int num_processes):
    """
    Optimize chunk size based on message size and number of processes.
    """
    cdef:
        int optimal_chunk_size
    
    # Base chunk size: 1MB
    optimal_chunk_size = 1024 * 1024
    
    # Adjust based on message size
    if message_size > 100 * 1024 * 1024:  # > 100MB
        optimal_chunk_size = 10 * 1024 * 1024  # 10MB chunks
    elif message_size > 10 * 1024 * 1024:  # > 10MB
        optimal_chunk_size = 5 * 1024 * 1024   # 5MB chunks
    
    # Adjust based on number of processes
    optimal_chunk_size = max(optimal_chunk_size // num_processes, 64 * 1024)  # Min 64KB
    
    return optimal_chunk_size


def combine_chunk_results(list results, object deserializer=None):
    """
    Combine the results from multiple chunks into a single result.
    
    Args:
        results: List of deserialized chunk results
        deserializer: The original deserializer function (for context)
    
    Returns:
        Combined result
    """
    if not results:
        return None
    
    # If only one result, return it directly
    if len(results) == 1:
        return results[0]
    
    # Filter out None results
    valid_results = [r for r in results if r is not None]
    
    if not valid_results:
        return None
    
    # Try to combine based on data types
    first_result = valid_results[0]
    
    # For dictionaries (JSON objects), merge them
    if isinstance(first_result, dict):
        combined = {}
        for result in valid_results:
            if isinstance(result, dict):
                combined.update(result)
        return combined
    
    # For lists/arrays, concatenate them
    elif isinstance(first_result, list):
        combined = []
        for result in valid_results:
            if isinstance(result, list):
                combined.extend(result)
        return combined
    
    # For strings, concatenate them
    elif isinstance(first_result, str):
        return ''.join(str(r) for r in valid_results)
    
    # For bytes, concatenate them
    elif isinstance(first_result, bytes):
        return b''.join(r for r in valid_results if isinstance(r, bytes))
    
    # For numeric types, sum them (or handle as needed)
    elif isinstance(first_result, (int, float)):
        return sum(valid_results)
    
    # For other types, return as list (fallback)
    else:
        return valid_results


# ============================================================================
# Async Multiprocessing Functions
# ============================================================================

async def async_multiprocessing_deserialize(bytes raw_message, object deserializer=None,
                                           int num_processes=0, int chunk_size=1024*1024,
                                           object loop=None):
    """
    Async wrapper for multiprocessing deserialization.
    This allows the deserialization to run in a process pool without blocking the event loop.
    """
    if loop is None:
        loop = asyncio.get_event_loop()
    
    # Run the multiprocessing deserialization in a thread pool
    # to avoid blocking the event loop
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=1) as executor:
        future = loop.run_in_executor(
            executor,
            multiprocessing_deserialize_cy,
            raw_message,
            deserializer,
            num_processes,
            chunk_size
        )
        return await future


def make_multiprocessing_deserializer(object sync_deserializer, bint use_chunking=True,
                                     int chunk_size=1024*1024):
    """
    Convert a synchronous deserializer to use multiprocessing.
    
    This wraps the sync deserializer to use multiprocessing for large messages.
    """
    if asyncio.iscoroutinefunction(sync_deserializer):
        # Already async, return as-is
        return sync_deserializer
    
    def multiprocessing_wrapper(raw_message):
        """Multiprocessing wrapper for sync deserializer."""
        if use_chunking and len(raw_message) > chunk_size:
            # Use multiprocessing for large messages
            return multiprocessing_deserialize_cy(raw_message, "custom", chunk_size=chunk_size)
        else:
            # Use original deserializer for small messages
            return sync_deserializer(raw_message)
    
    return multiprocessing_wrapper


async def make_async_multiprocessing_deserializer(object sync_deserializer, 
                                                 bint use_chunking=True,
                                                 int chunk_size=1024*1024,
                                                 object loop=None):
    """
    Convert a synchronous deserializer to an async function that uses multiprocessing.
    """
    if asyncio.iscoroutinefunction(sync_deserializer):
        # Already async, return as-is
        return sync_deserializer
    
    async def async_multiprocessing_wrapper(raw_message):
        """Async multiprocessing wrapper for sync deserializer."""
        nonlocal loop
        if loop is None:
            loop = asyncio.get_event_loop()
            
        if use_chunking and len(raw_message) > chunk_size:
            # Use multiprocessing for large messages
            return await async_multiprocessing_deserialize(
                raw_message, sync_deserializer, chunk_size=chunk_size, loop=loop
            )
        else:
            # Use thread pool for small messages
            return await loop.run_in_executor(None, sync_deserializer, raw_message)
    
    return async_multiprocessing_wrapper


def batch_multiprocessing_deserialize(list raw_messages, object deserializer=None,
                                     int num_processes=0, int chunk_size=1024*1024):
    """
    Deserialize multiple messages using multiprocessing.
    
    Args:
        raw_messages: List of raw bytes messages
        deserializer: The deserializer function to use (or None for raw bytes)
        num_processes: Number of processes to use
        chunk_size: Size of each chunk in bytes
    
    Returns:
        List of deserialized messages
    """
    if num_processes == 0:
        num_processes = mp.cpu_count()
    
    # Determine if we should use chunking based on message sizes
    total_size = sum(len(msg) for msg in raw_messages)
    use_chunking = total_size > chunk_size * num_processes
    
    if use_chunking:
        # Use chunking for large messages
        results = []
        for message in raw_messages:
            result = multiprocessing_deserialize_cy(
                message, deserializer, num_processes, chunk_size
            )
            results.append(result)
        return results
    else:
        # Process each message as a whole
        with Pool(processes=num_processes) as pool:
            # Create arguments for each message
            message_args = [(msg, deserializer) for msg in raw_messages]
            
            # Process messages in parallel
            results = pool.starmap(deserialize_chunk_cy, message_args)
        
        return results


async def async_batch_multiprocessing_deserialize(list raw_messages, object deserializer=None,
                                                 int num_processes=0, int chunk_size=1024*1024,
                                                 object loop=None):
    """
    Async wrapper for batch multiprocessing deserialization.
    """
    if loop is None:
        loop = asyncio.get_event_loop()
    
    from concurrent.futures import ThreadPoolExecutor
    with ThreadPoolExecutor(max_workers=1) as executor:
        future = loop.run_in_executor(
            executor,
            batch_multiprocessing_deserialize,
            raw_messages,
            deserializer,
            num_processes,
            chunk_size
        )
        return await future





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
    queue = asyncio.Queue(maxsize=1)

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


if PY_MAJOR_VERSION >= 3 and PY_MINOR_VERSION >= 7:
    def get_working_loop():
        """Returns a running event loop.

        Due to a defect of asyncio.get_event_loop, its returned event loop might
        not be set as the default event loop for the main thread.
        """
        try:
            return asyncio.get_running_loop()
        except RuntimeError:
            with warnings.catch_warnings():
                # Convert DeprecationWarning to errors so we can capture them with except
                warnings.simplefilter("error", DeprecationWarning)
                try:
                    return asyncio.get_event_loop_policy().get_event_loop()
                # Since version 3.12, DeprecationWarning is emitted if there is no
                # current event loop.
                except DeprecationWarning:
                    return asyncio.get_event_loop_policy().new_event_loop()
else:
    def get_working_loop():
        """Returns a running event loop."""
        return asyncio.get_event_loop()


def raise_if_not_valid_trailing_metadata(object metadata):
    if not hasattr(metadata, '__iter__') or isinstance(metadata, dict):
        raise TypeError(f'Invalid trailing metadata type, expected {TYPE_METADATA_STRING}: {metadata}')
    for item in metadata:
        if not isinstance(item, tuple):
            raise TypeError(f'Invalid trailing metadata type, expected {TYPE_METADATA_STRING}: {metadata}')
        if len(item) != 2:
            raise TypeError(f'Invalid trailing metadata type, expected {TYPE_METADATA_STRING}: {metadata}')
        if not isinstance(item[0], str):
            raise TypeError(f'Invalid trailing metadata type, expected {TYPE_METADATA_STRING}: {metadata}')
        if not isinstance(item[1], str) and not isinstance(item[1], bytes):
            raise TypeError(f'Invalid trailing metadata type, expected {TYPE_METADATA_STRING}: {metadata}')
