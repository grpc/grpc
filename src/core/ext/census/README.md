<!---
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

