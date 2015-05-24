# gRPC for Objective-C

## How to generate a client library from a Protocol Buffers definition

First install v3 of the Protocol Buffers compiler (_protoc_), by cloning [its Git repository](https://github.com/google/protobuf) and following these [installation instructions](https://github.com/google/protobuf#c-installation---unix) (the ones titled C++; don't miss the note for Mac users).

Then clone this repository and execute the following commands from the root directory where it was cloned.

Compile the gRPC plugins for _protoc_:
```sh
make plugins
```

Create a symbolic link to the compiled plugin binary somewhere in your `$PATH`:
```sh
ln -s `pwd`/bins/opt/grpc_objective_c_plugin /usr/local/bin/protoc-gen-objcgrpc
```
(Notice that the name of the created link must begin with "protoc-gen-" for _protoc_ to recognize it as a plugin).

If you don't want to create the symbolic link, you can alternatively copy the binary (with the appropriate name). Or you might prefer instead to specify the plugin's path as a flag when invoking _protoc_, in which case no system modification nor renaming is necessary.

Finally, run _protoc_ with the following flags to generate the client library for your `.proto` files:

```sh
protoc --objc_out=. --objcgrpc_out=. *.proto
```

This will generate a pair of `.pbobjc.h`/`.pbobjc.m` files for each `.proto` file, with the messages and enums defined in them. And a pair of `.pbrpc.h`/`.pbrpc.m` files for each `.proto` file with services defined. The latter contains the code to make remote calls to the specified API.

## How to integrate a generated gRPC library in your project

### If you use Cocoapods

This is the recommended approach.

You need to create a Podspec file for the generated library. This is simply a matter of copying an example like [this one](https://github.com/grpc/grpc/blob/master/src/objective-c/examples/Sample/RemoteTestClient/RemoteTest.podspec) to the directory where the source files were generated. Update the name and other metadata of the Podspec as suitable.

Once your library has a Podspec, refer to it from your Podfile using `:path` as described [here](https://guides.cocoapods.org/using/the-podfile.html#using-the-files-from-a-folder-local-to-the-machine).

### If you don't use Cocoapods

You need to compile the generated `.pbpbjc.*` files (the enums and messages) without ARC support, and the generated `.pbrpc.*` files (the services) with ARC support. The generated code depends on v0.3+ of the Objective-C gRPC runtime library and v3.0+ of the Objective-C Protobuf runtime library.

These libraries need to be integrated into your project as described in their respective Podspec files:

* [Podspec](https://github.com/grpc/grpc/blob/master/gRPC.podspec) for the Objective-C gRPC runtime library. This can be tedious to configure manually.
* [Podspec](https://github.com/jcanizales/protobuf/blob/add-podspec/Protobuf.podspec) for the Objective-C Protobuf runtime library.
