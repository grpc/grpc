<!---
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-->

# Census - a resource measurement and tracing system

This directory contains code for Census, which will ultimately provide the
following features for any gRPC-using system:
* A [dapper](http://research.google.com/pubs/pub36356.html)-like tracing
  system, enabling tracing across a distributed infrastructure.
* RPC statistics and measurements for key metrics, such as latency, bytes
  transferred, number of errors etc.
* Resource measurement framework which can be used for measuring custom
  metrics. Through the use of [tags](#Tags), these can be broken down across
  the entire distributed stack.
* Easy integration of the above with
  [Google Cloud Trace](https://cloud.google.com/tools/cloud-trace) and
  [Google Cloud Monitoring](https://cloud.google.com/monitoring/).

## Concepts

### Context

### Operations

### Tags

### Metrics

## API

### Internal/RPC API

### External/Client API

### RPC API

## Files in this directory

Note that files and functions in this directory can be split into two
categories:
* Files that define core census library functions. Functions etc. in these
  files are named census\_\*, and constitute the core census library
  functionality. At some time in the future, these will become a standalone
  library.
* Files that define functions etc. that provide a convenient interface between
  grpc and the core census functionality. These files are all named
  grpc\_\*.{c,h}, and define function names beginning with grpc\_census\_\*.

