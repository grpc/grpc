[![npm](https://img.shields.io/npm/v/grpc.svg)](https://www.npmjs.com/package/grpc)
# Node.js gRPC Library

## PREREQUISITES
- `node`: This requires `node` to be installed, version `4.0` or above. If you instead have the `nodejs` executable on Debian, you should install the [`nodejs-legacy`](https://packages.debian.org/sid/nodejs-legacy) package.

- **Note:** If you installed `node` via a package manager and the version is still less than `4.0`, try directly installing it from [nodejs.org](https://nodejs.org).

## INSTALLATION

Install the gRPC NPM package

```sh
npm install grpc
```

## BUILD FROM SOURCE
 1. Clone [the grpc Git Repository](https://github.com/grpc/grpc).
 2. Run `npm install --build-from-source` from the repository root.

 - **Note:** On Windows, this might fail due to [nodejs issue #4932](https://github.com/nodejs/node/issues/4932) in which case, you will see something like the following in `npm install`'s output (towards the very beginning):

    ```
     ..
     Building the projects in this solution one at a time. To enable parallel build, please add the "/m" switch.
     WINDOWS_BUILD_WARNING
      "..\IMPORTANT: Due to https:\github.com\nodejs\node\issues\4932, to build this library on Windows, you must first remove C:\Users\jenkins\.node-gyp\4.4.0\include\node\openssl"
      ...
      ..
    ```

    To fix this, you will have to delete the folder `C:\Users\<username>\.node-gyp\<node_version>\include\node\openssl` and retry `npm install`


## TESTING
To run the test suite, simply run `npm test` in the install location.
