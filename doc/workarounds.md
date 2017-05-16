# gRPC Server Backward Compatibility Issues and Workarounds Manageent

## Introduction
This document lists the workarounds implemented on gRPC servers for record and reference when users need to enable a certain workaround.

## Workaround Lists

| Workaround ID                       | Date added   | Issue                                             | Workaround Description                               | Status                   |
|-------------------------------------|--------------|---------------------------------------------------|------------------------------------------------------|--------------------------|
| WORKAROUND\_ID\_CRONET\_COMPRESSION | May 06, 2017 | Before version v1.3.0-dev, gRPC iOS client's      | Implemented as a server channel filter in C core.    | Implemented in C and C++ |
|                                     |              | Cronet transport did not implement compression.   | The filter identifies the version of peer client     |                          |
|                                     |              | However the clients still claim to support        | with incoming `user-agent` header of each call. If   |                          |
|                                     |              | compression. As a result, a client fails to parse | the client's gRPC version is lower that or equal to  |                          |
|                                     |              | received message when the message is compressed.  | v1.3.x, a flag GRPC\_WRITE\_NO\_COMPRESS is marked   |                          |
|                                     |              |                                                   | for all send\_message ops which prevents compression |                          |
|                                     |              | The problem above was resolved in gRPC v1.3.0-dev.| of the messages to be sent out.                      |                          |
|                                     |              | For backward compatibility, a server must         |                                                      |                          |
|                                     |              | forcingly disable compression for gRPC clients of |                                                      |                          |
|                                     |              | version lower than or equal to v1.3.0-dev.        |                                                      |                          |

