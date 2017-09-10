module HelloWorld.Server

// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

open System
open System.Threading.Tasks
open Grpc.Core
open Helloworld

type GreeterImpl() =
  inherit Greeter.GreeterBase()
    override x.SayHello (request, context) =
      Task.FromResult(HelloReply(Message = sprintf "Hello %s" request.Name))

let port = 50051us

[<EntryPoint>]
let main argv =
  let server = Server()
  let listening = server.Ports.Add(ServerPort("localhost", int port, ServerCredentials.Insecure))
  server.Services.Add(Greeter.BindService(GreeterImpl()))
  server.Start()
  printfn "Greeter server started on %i and is listening on port %u" port listening
  printfn "Press any key to stop the server..."
  Console.ReadKey true |> ignore
  server.ShutdownAsync().Wait()
  0