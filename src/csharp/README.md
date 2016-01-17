[![Nuget](https://img.shields.io/nuget/v/Grpc.svg)](http://www.nuget.org/packages/Grpc/)
gRPC C#
=======

A C# implementation of gRPC.

Status
------

Beta

PREREQUISITES
--------------

- Windows: .NET Framework 4.5+, Visual Studio 2013 or 2015.
- Linux: Mono 3.2.8+, MonoDevelop 5.9 with NuGet add-in installed.
- Mac OS X: [homebrew][], Xamarin Studio with NuGet add-in installed.

HOW TO USE
--------------

**Windows**

- Open Visual Studio and start a new project/solution.

- Add NuGet package `Grpc` as a dependency (Project options -> Manage NuGet Packages).
  That will also pull all the transitive dependencies (including the native libraries that
  gRPC C# is using internally).

**Linux (Debian)**

- Add [Debian jessie-backports][] to your `sources.list` file. Example:

  ```sh
  echo "deb http://http.debian.net/debian jessie-backports main" | \
  sudo tee -a /etc/apt/sources.list
  ```

- Install the gRPC Debian package

  ```sh
  sudo apt-get update
  sudo apt-get install libgrpc0
  ```

- gRPC C# depends on native shared library `libgrpc_csharp_ext.so` (Unix flavor of grpc_csharp_ext.dll).
  This library is not part of the base gRPC debian package and needs to be installed manually from
  a `.deb` file. Download the debian package `libgrpc_csharp_ext` from corresponding gRPC release on GitHub
  and install it using `dpkg`.

  ```sh
  # choose version corresponding to the version of libgrpc you've installed.
  wget https://github.com/grpc/grpc/releases/download/release-0_11_0/libgrpc-csharp-ext0_0.11.0.0-1_amd64.deb
  dpkg -i libgrpc-csharp-ext0_0.11.0.0-1_amd64.deb
  ```

- Open MonoDevelop and start a new project/solution.

- Add NuGet package `Grpc` as a dependency (Project -> Add NuGet packages).

- NOTE: Currently, there are no debian packages for the latest version Protocol Buffers compiler (_protoc_)
  and the gRPC _protoc_ plugin. You can install them using [gRPC Linuxbrew instructions][].

**Mac OS X**

- WARNING: As of now gRPC C# only works on 64bit version of Mono (because we don't compile
  the native extension for C# in 32bit mode yet). That means your development experience
  with Xamarin Studio on MacOS will not be great, as you won't be able to run your
  code directly from Xamarin Studio (which requires 32bit version of Mono).

- Install [homebrew][]. Run the following command to install gRPC C# native dependencies.

  ```sh
  $ curl -fsSL https://goo.gl/getgrpc | bash -
  ```
  This will download and run the [gRPC install script][], then install the latest version of gRPC C core and native C# extension.
  It also installs Protocol Buffers compiler (_protoc_) and the gRPC _protoc_ plugin for C#.

- Install 64-bit version of mono with command `brew install mono`.

- Open Xamarin Studio and start a new project/solution.

- Add NuGet package `Grpc` as a dependency (Project -> Add NuGet packages).

- *You will be able to build your project in Xamarin Studio, but to run or test it,
  you will need to run it under 64-bit version of Mono.*

BUILD FROM SOURCE
-----------------

You only need to go through these steps if you are planning to develop gRPC C#.
If you are a user of gRPC C#, go to Usage section above.

**Windows**

- The grpc_csharp_ext native library needs to be built so you can build the gRPC C# solution. You can 
  either build the native solution in `vsprojects/grpc.sln` from Visual Studio manually, or you can use
  a convenience batch script that builds everything for you.

  ```
  > REM From src/csharp directory
  > buildall.bat
  ```

- Open Grpc.sln using Visual Studio. NuGet dependencies will be restored
  upon build (you need to have NuGet add-in installed).

**Linux**

  ```sh
  $ sudo apt-get install mono-devel
  $ sudo apt-get install nunit nunit-console
  ```

You can use older versions of MonoDevelop, but then you might need to restore
NuGet dependencies manually (by `nuget restore`), because older versions of MonoDevelop
don't support NuGet add-in.

- Compile and install the gRPC C# extension library (that will be used via
  P/Invoke from C#).
  ```sh
  $ make grpc_csharp_ext
  $ sudo make install_grpc_csharp_ext
  ```

- Use MonoDevelop to open the solution Grpc.sln

- Build the solution & run all the tests from test view.

RUNNING TESTS
-------------

gRPC C# is using NUnit as the testing framework.

Under Visual Studio, make sure NUnit test adapter is installed (under "Extensions and Updates").
Then you should be able to run all the tests using Test Explorer.

Under Monodevelop, make sure you installed "NUnit support" in Add-in manager.
Then you should be able to run all the test from the Test View.

After building the solution, you can also run the tests from command line 
using nunit-console tool.

```sh
# from Grpc.Core.Test/bin/Debug directory
$ nunit-console Grpc.Core.Tests.dll
```

gRPC team uses a Python script to simplify facilitate running tests for
different languages.

```
tools/run_tests/run_tests.py -l csharp
```

DOCUMENTATION
-------------
- the gRPC C# reference documentation is available online at [grpc.io][]
- [Helloworld example][]

CONTENTS
--------

- ext:
  The extension library that wraps C API to be more digestible by C#.
- Grpc.Auth:
  gRPC OAuth2 support.
- Grpc.Core:
  The main gRPC C# library.
- Grpc.Examples:
  API examples for math.proto
- Grpc.Examples.MathClient:
  An example client that sends some requests to math server.
- Grpc.Examples.MathServer:
  An example client that sends some requests to math server.
- Grpc.IntegrationTesting:
  Cross-language gRPC implementation testing (interop testing).

TROUBLESHOOTING
---------------

### Problem: Unable to load DLL 'grpc_csharp_ext.dll'

Internally, gRPC C# uses a native library written in C (gRPC C core) and invokes its functionality via P/Invoke. `grpc_csharp_ext` library is a native extension library that facilitates this by wrapping some C core API into a form that's more digestible for P/Invoke. If you get the above error, it means that the native dependencies could not be located by the C# runtime (or they are incompatible with the current runtime, so they could not be loaded). The solution to this is environment specific.

- If you are developing on Windows in Visual Studio, the `grpc_csharp_ext.dll` that is shipped by gRPC nuget packages should be automatically copied to your build destination folder once you build. By adjusting project properties in your VS project file, you can influence which exact configuration of `grpc_csharp_ext.dll` will be used (based on VS version, bitness, debug/release configuration).

- If you are running your application that is using gRPC on Windows machine that doesn't have Visual Studio installed, you might need to install [Visual C++ 2013 redistributable](https://www.microsoft.com/en-us/download/details.aspx?id=40784) that contains some system .dll libraries that `grpc_csharp_ext.dll` depends on (see #905 for more details).

- On Linux (or Docker), you need to first install gRPC C core and `libgrpc_csharp_ext.so` shared libraries.
  See [How to Use](#how-to-use) section for details how to install it.
  Installation on a machine where your application is going to be deployed is no different.

- On Mac, you need to first install gRPC C core and `libgrpc_csharp_ext.dylib` shared libraries using Homebrew. See above for installation instruction.
  Installation on a machine where your application is going to be deployed is no different.

- Possible cause for the problem is that the `grpc_csharp_ext` library is installed, but it has different bitness (32/64bit) than your C# runtime (in case you are using mono) or C# application.

[gRPC Linuxbrew instructions]:https://github.com/grpc/homebrew-grpc#quick-install-linux
[homebrew]:http://brew.sh
[gRPC install script]:https://raw.githubusercontent.com/grpc/homebrew-grpc/master/scripts/install
[grpc.io]: http://www.grpc.io/docs/installation/csharp.html
[Debian jessie-backports]:http://backports.debian.org/Instructions/
[Helloworld example]:../../examples/csharp/helloworld
