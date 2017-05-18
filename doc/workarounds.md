# gRPC Server Backward Compatibility Issues and Workarounds Manageent

## Introduction
This document lists the workarounds implemented on gRPC servers for record and reference when users need to enable a certain workaround.

## Workaround List

### Cronet Compression

**Workaround ID:** WORKAROUND\_ID\_CRONET\_COMPRESSION

**Date added:** May 06, 2017

**Status:** Implemented in C core and C++

**Issue:** Before version v1.3.0-dev, gRPC iOS client's Cronet transport did not implement compression. However the clients still claim to support compression. As a result, a client fails to parse received message when the message is compressed.
The problem above was resolved in gRPC v1.3.0-dev. For backward compatibility, a server must forcingly disable compression for gRPC clients of version lower than or equal to v1.3.0-dev.

**Workaround Description:** Implemented as a server channel filter in C core.  The filter identifies the version of peer client with incoming `user-agent` header of each call. If the client's gRPC version is lower that or equal to v1.3.x, a flag GRPC_WRITE_NO_COMPRESS is marked for all send_message ops which prevents compression of the messages to be sent out.
