#region Copyright notice and license
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
#endregion

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Grpc.Core;
using Grpc.Core.Utils;
using Grpc.Reflection.V1Alpha;
using Google.Protobuf.Reflection;

namespace Grpc.Reflection
{
    /// <summary>
    /// Implementation of server reflection service.
    /// </summary>
    public class ReflectionServiceImpl : Grpc.Reflection.V1Alpha.ServerReflection.ServerReflectionBase
    {
        readonly List<string> services;
        readonly SymbolRegistry symbolRegistry;

        /// <summary>
        /// Creates a new instance of <c>ReflectionServiceIml</c>.
        /// </summary>
        public ReflectionServiceImpl(IEnumerable<string> services, SymbolRegistry symbolRegistry)
        {
            this.services = new List<string>(services);
            this.symbolRegistry = symbolRegistry;
        }

        /// <summary>
        /// Creates a new instance of <c>ReflectionServiceIml</c>.
        /// </summary>
        public ReflectionServiceImpl(IEnumerable<ServiceDescriptor> serviceDescriptors)
        {
            this.services = new List<string>(serviceDescriptors.Select((serviceDescriptor) => serviceDescriptor.FullName));
            this.symbolRegistry = SymbolRegistry.FromFiles(serviceDescriptors.Select((serviceDescriptor) => serviceDescriptor.File));
        }

        /// <summary>
        /// Creates a new instance of <c>ReflectionServiceIml</c>.
        /// </summary>
        public ReflectionServiceImpl(params ServiceDescriptor[] serviceDescriptors) : this((IEnumerable<ServiceDescriptor>) serviceDescriptors)
        {
        }

        public override async Task ServerReflectionInfo(IAsyncStreamReader<ServerReflectionRequest> requestStream, IServerStreamWriter<ServerReflectionResponse> responseStream, ServerCallContext context)
        {
            while (await requestStream.MoveNext())
            {
                var response = ProcessRequest(requestStream.Current);
                await responseStream.WriteAsync(response);
            }
        }

        ServerReflectionResponse ProcessRequest(ServerReflectionRequest request)
        {
            switch (request.MessageRequestCase)
            {
                case ServerReflectionRequest.MessageRequestOneofCase.FileByFilename:
                    return FileByFilename(request.FileByFilename);
                case ServerReflectionRequest.MessageRequestOneofCase.FileContainingSymbol:
                    return FileContainingSymbol(request.FileContainingSymbol);
                case ServerReflectionRequest.MessageRequestOneofCase.ListServices:
                    return ListServices();
                case ServerReflectionRequest.MessageRequestOneofCase.AllExtensionNumbersOfType:
                case ServerReflectionRequest.MessageRequestOneofCase.FileContainingExtension:
                default:
                    return CreateErrorResponse(StatusCode.Unimplemented, "Request type not supported by C# reflection service.");
            }
        }

        ServerReflectionResponse FileByFilename(string filename)
        {
            FileDescriptor file = symbolRegistry.FileByName(filename);
            if (file == null)
            {
                return CreateErrorResponse(StatusCode.NotFound, "File not found.");
            }

            var transitiveDependencies = new HashSet<FileDescriptor>();
            CollectTransitiveDependencies(file, transitiveDependencies);

            return new ServerReflectionResponse
            {
                FileDescriptorResponse = new FileDescriptorResponse { FileDescriptorProto = { transitiveDependencies.Select((d) => d.SerializedData) } }
            };
        }

        ServerReflectionResponse FileContainingSymbol(string symbol)
        {
            FileDescriptor file = symbolRegistry.FileContainingSymbol(symbol);
            if (file == null)
            {
                return CreateErrorResponse(StatusCode.NotFound, "Symbol not found.");
            }

            var transitiveDependencies = new HashSet<FileDescriptor>();
            CollectTransitiveDependencies(file, transitiveDependencies);

            return new ServerReflectionResponse
            {
                FileDescriptorResponse = new FileDescriptorResponse { FileDescriptorProto = { transitiveDependencies.Select((d) => d.SerializedData) } }
            };
        }

        ServerReflectionResponse ListServices()
        {
            var serviceResponses = new ListServiceResponse();
            foreach (string serviceName in services)
            {
                serviceResponses.Service.Add(new ServiceResponse { Name = serviceName });
            }

            return new ServerReflectionResponse
            {
                ListServicesResponse = serviceResponses
            };
        }

        ServerReflectionResponse CreateErrorResponse(StatusCode status, string message)
        {
            return new ServerReflectionResponse
            {
                ErrorResponse = new ErrorResponse { ErrorCode = (int) status, ErrorMessage = message }
            };
        }

        void CollectTransitiveDependencies(FileDescriptor descriptor, HashSet<FileDescriptor> pool)
        {
            pool.Add(descriptor);
            foreach (var dependency in descriptor.Dependencies)
            {
                if (pool.Add(dependency))
                {
                    // descriptors cannot have circular dependencies
                    CollectTransitiveDependencies(dependency, pool);
                }
            }
        }
    }
}
