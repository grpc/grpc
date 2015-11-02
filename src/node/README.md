[![npm](https://img.shields.io/npm/v/grpc.svg)](https://www.npmjs.com/package/grpc)
# Node.js gRPC Library

## Status
Beta

## PREREQUISITES
- `node`: This requires `node` to be installed. If you instead have the `nodejs` executable on Debian, you should install the [`nodejs-legacy`](https://packages.debian.org/sid/nodejs-legacy) package.

## INSTALLATION

Install the gRPC NPM package

```sh
npm install grpc
```

## BUILD FROM SOURCE
 1. Clone [the grpc Git Repository](https://github.com/grpc/grpc).
 3. Run `npm install`.

## TESTING
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
function Server([serverOpions])
```

Constructs a server to which service/implementation pairs can be added.


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

An object with factory methods for creating credential objects for servers.
