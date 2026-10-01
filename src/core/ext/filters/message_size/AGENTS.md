# Message Size Filter

This directory contains the implementation of the message size filter, which is
a channel filter that enforces limits on the size of incoming and outgoing
messages.

## Overarching Purpose

The message size filter can be used to prevent clients and servers from sending
or receiving messages that are too large, which can help to prevent
denial-of-service attacks and other resource exhaustion issues.

## Files

*   **`message_size_filter.h`, `message_size_filter.cc`**: These files
    contain the implementation of the `MessageSizeFilter`, which is a channel
    filter that checks the size of each message against the configured limits.

## Major Classes

*   **`grpc_core::MessageSizeFilter`**: The channel filter implementation.
*   **`grpc_core::MessageSizeParsedConfig`**: A class that holds the parsed
    message size limits from the service config or channel arguments.

## Notes

*   The message size limits can be configured on both the client and the
    server.
*   The limits can be set via channel arguments (e.g.,
    `GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH`) or through the service config.
*   If a message exceeds the configured limit, the filter will fail the RPC
    with a `RESOURCE_EXHAUSTED` status.
