# Message Compression Filter

This directory contains the implementation of the message compression filter.

## Overarching Purpose

The message compression filter is responsible for compressing and decompressing gRPC messages. It can be used on both the client and the server to reduce the amount of data sent over the network.

## Files

*   `compression_filter.h`, `compression_filter.cc`: These files define the `ClientCompressionFilter` and `ServerCompressionFilter` classes, which are client-side and server-side channel filters, respectively. They also define the `ChannelCompression` class, which encapsulates the core compression and decompression logic.

## Major Classes

*   `grpc_core::ClientCompressionFilter`: The client-side channel filter for compression.
*   `grpc_core::ServerCompressionFilter`: The server-side channel filter for compression.
*   `grpc_core::ChannelCompression`: A helper class that encapsulates the logic for compressing and decompressing messages, as well as for handling compression-related metadata.

## Notes

*   The compression filter supports various compression algorithms, such as gzip and deflate. The desired compression algorithm can be specified through channel arguments or on a per-call basis via metadata.
*   The filter also supports disabling compression for individual messages, which can be useful for preventing security vulnerabilities like CRIME and BEAST.
*   This filter is an important part of gRPC's performance story, as it can significantly reduce network bandwidth usage and improve latency.
