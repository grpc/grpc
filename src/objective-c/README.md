# gRPC for Objective-C

- [Install protoc with the gRPC plugin](#install)
- [Use protoc to generate a gRPC library](#protoc)
- [Integrate the generated gRPC library in your project](#cocoapods)
- [Use the generated library in your code](#use)
- [Alternative methods](#alternatives)
	- [Install protoc and the gRPC plugin without using Homebrew](#nohomebrew)
	- [Integrate the generated gRPC library without using Cocoapods](#nococoapods)

<a name="install"></a>
## Install protoc with the gRPC plugin

On Mac OS X, install [homebrew][]. On Linux, install [linuxbrew][].

Run the following command to install the Protocol Buffers compiler (_protoc_) and the gRPC _protoc_ plugin:
```sh
$ curl -fsSL https://goo.gl/getgrpc | bash -
```
This will download and run the [gRPC install script][]. After the command completes, you're ready to proceed.

<a name="protoc"></a>
## Use protoc to generate a gRPC library

Run _protoc_ with the following flags to generate the client library for your `.proto` files:

```sh
protoc --objc_out=. --objcgrpc_out=. *.proto
```

This will generate a pair of `.pbobjc.h`/`.pbobjc.m` files for each `.proto` file, with the messages and enums defined in them. And a pair of `.pbrpc.h`/`.pbrpc.m` files for each `.proto` file with services defined. The latter contains the code to make remote calls to the specified API.

<a name="cocoapods"></a>
## Integrate the generated gRPC library in your project

Install [Cocoapods](https://cocoapods.org/#install).

You need to create a Podspec file for the generated library. You may simply copy the following example to the directory where the source files were generated, updating the name and other metadata of the Podspec as necessary:

```ruby
Pod::Spec.new do |s|
  s.name     = '<Podspec file name>'
  s.version  = '...'
  s.summary  = 'Client library to make RPCs to <my proto API>'
  s.homepage = '...'
  s.license  = '...'
  s.authors  = { '<my name>' => '<my email>' }

  s.ios.deployment_target = '6.0'
  s.osx.deployment_target = '10.8'

  s.subspec 'Messages' do |ms|
    ms.source_files = '*.pbobjc.{h,m}'
    ms.requires_arc = false
    ms.dependency 'Protobuf', '~> 3.0'
  end

  s.subspec 'Services' do |ss|
    ss.source_files = '*.pbrpc.{h,m}'
    ss.requires_arc = true
    ss.dependency 'gRPC', '~> 0.0'
    ss.dependency '<Podspec file name>/Messages'
  end
end
```

The file should be named `<Podspec file name>.podspec`. You can refer to this [example Podspec][]. Once your library has a Podspec, Cocoapods can install it into any XCode project. For that, go into your project's directory and create a Podfile by running:

```sh
pod init
```

Next add a line to your Podfile to refer to your library's Podspec. Use `:path` as described [here](https://guides.cocoapods.org/using/the-podfile.html#using-the-files-from-a-folder-local-to-the-machine):

```ruby
pod '<Podspec file name>', :path => 'path/to/the/directory/of/your/podspec'
```

You can look at this [example Podfile][].

Finally, in your project's directory, run:

```sh
pod install
```

<a name="use"></a>
## Use the generated library in your code

Please check this [sample app][] for examples of how to use a generated gRPC library.

<a name="alternatives"></a>
## Alternative methods

<a name="nohomebrew"></a>
### Install protoc and the gRPC plugin without using Homebrew

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

<a name="nococoapods"></a>
### Integrate the generated gRPC library without using Cocoapods

You need to compile the generated `.pbpbjc.*` files (the enums and messages) without ARC support, and the generated `.pbrpc.*` files (the services) with ARC support. The generated code depends on v0.3+ of the Objective-C gRPC runtime library and v3.0+ of the Objective-C Protobuf runtime library.

These libraries need to be integrated into your project as described in their respective Podspec files:

* [Podspec](https://github.com/grpc/grpc/blob/master/gRPC.podspec) for the Objective-C gRPC runtime library. This can be tedious to configure manually.
* [Podspec](https://github.com/jcanizales/protobuf/blob/add-podspec/Protobuf.podspec) for the Objective-C Protobuf runtime library.

[homebrew]:http://brew.sh
[linuxbrew]:https://github.com/Homebrew/linuxbrew
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[example Podspec]:https://github.com/grpc/grpc/blob/master/src/objective-c/examples/Sample/RemoteTestClient/RemoteTest.podspec
[example Podfile]:https://github.com/grpc/grpc/blob/master/src/objective-c/examples/Sample/Podfile
[sample app]: https://github.com/grpc/grpc/tree/master/src/objective-c/examples/Sample
