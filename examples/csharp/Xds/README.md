gRPC Hostname example (C#)
========================

BACKGROUND
-------------
This is a version of the helloworld example with a server whose response includes its hostname. It also supports health and reflection services. This makes it a good server to test infrastructure, like load balancing.

PREREQUISITES
-------------

- The [.NET Core SDK 2.1+](https://www.microsoft.com/net/core)

You can also build the solution `Greeter.sln` using Visual Studio 2019,
but it's not a requirement.

BUILD AND RUN
-------------

- Build and run the server

  ```
  > dotnet run -p GreeterServer
  ```

- Build and run the client

  ```
  > dotnet run -p GreeterClient
  ```
