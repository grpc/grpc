# Node.js gRPC Library

## Status

Alpha : Ready for early adopters

## Prerequisites

This requires `node` to be installed. If you instead have the `nodejs` executable on Debian, you should install the [`nodejs-legacy`](https://packages.debian.org/sid/nodejs-legacy) package.

## Installation

 1. Clone [the grpc repository](https://github.com/grpc/grpc).
 2. Follow the instructions in the `INSTALL` file in the root of that repository to install the C core library that this package depends on.
 3. Run `npm install`.

If you install the gRPC C core library in a custom location, then you need to set some environment variables to install this library. The command will look like this:

```sh
CXXFLAGS=-I<custom location>/include LDFLAGS=-L<custom location>/lib npm install [grpc]
```

## Tests

To run the test suite, simply run `npm test` in the install location.

## API

This library internally uses [ProtoBuf.js](https://github.com/dcodeIO/ProtoBuf.js), and some structures it exports match those exported by that library

If you require this module, you will get an object with the following members

```javascript
function load(filename)
```

Takes a filename of a [Protocol Buffer](https://developers.google.com/protocol-buffers/) file, and returns an object representing the structure of the protocol buffer in the following way:

 - Namespaces become maps from the names of their direct members to those member objects
 - Service definitions become client constructors for clients for that service. They also have a `service` member that can be used for constructing servers.
 - Message definitions become Message constructors like those that ProtoBuf.js would create
 - Enum definitions become Enum objects like those that ProtoBuf.js would create
 - Anything else becomes the relevant reflection object that ProtoBuf.js would create


```javascript
function loadObject(reflectionObject)
```

Returns the same structure that `load` returns, but takes a reflection object from `ProtoBuf.js` instead of a file name.

```javascript
function buildServer(serviceArray)
```

Takes an array of service objects and returns a constructor for a server that handles requests to all of those services.


```javascript
status
```

An object mapping status names to status code numbers.


```javascript
callError
```

An object mapping call error names to codes. This is primarily useful for tracking down certain kinds of internal errors.


```javascript
Credentials
```

An object with factory methods for creating credential objects for clients.


```javascript
ServerCredentials
```

An object with factory methods fro creating credential objects for servers.
