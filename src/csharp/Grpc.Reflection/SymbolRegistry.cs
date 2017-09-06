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

using System.Collections.Generic;
using Grpc.Core.Utils;
using Google.Protobuf.Reflection;

namespace Grpc.Reflection
{
    /// <summary>Registry of protobuf symbols</summary>
    public class SymbolRegistry
    {
        private readonly Dictionary<string, FileDescriptor> filesByName;
        private readonly Dictionary<string, FileDescriptor> filesBySymbol;
        
        private SymbolRegistry(Dictionary<string, FileDescriptor> filesByName, Dictionary<string, FileDescriptor> filesBySymbol)
        {
            this.filesByName = new Dictionary<string, FileDescriptor>(filesByName);
            this.filesBySymbol = new Dictionary<string, FileDescriptor>(filesBySymbol);
        }

        /// <summary>
        /// Creates a symbol registry from the specified set of file descriptors.
        /// </summary>
        /// <param name="fileDescriptors">The set of files to include in the registry. Must not contain null values.</param>
        /// <returns>A symbol registry for the given files.</returns>
        public static SymbolRegistry FromFiles(IEnumerable<FileDescriptor> fileDescriptors)
        {
            GrpcPreconditions.CheckNotNull(fileDescriptors);
            var builder = new Builder();
            foreach (var file in fileDescriptors)
            {
                builder.AddFile(file);
            }
            return builder.Build();
        }

        /// <summary>
        /// Gets file descriptor for given file name (including package path). Returns <c>null</c> if not found.
        /// </summary>
        public FileDescriptor FileByName(string filename)
        {
            FileDescriptor file;
            filesByName.TryGetValue(filename, out file);
            return file;
        }

        /// <summary>
        /// Gets file descriptor that contains definition of given symbol full name (including package path). Returns <c>null</c> if not found.
        /// </summary>
        public FileDescriptor FileContainingSymbol(string symbol)
        {
            FileDescriptor file;
            filesBySymbol.TryGetValue(symbol, out file);
            return file;
        }

        /// <summary>
        /// Builder class which isn't exposed, but acts as a convenient alternative to passing round two dictionaries in recursive calls.
        /// </summary>
        private class Builder
        {
            private readonly Dictionary<string, FileDescriptor> filesByName;
            private readonly Dictionary<string, FileDescriptor> filesBySymbol;
            

            internal Builder()
            {
                filesByName = new Dictionary<string, FileDescriptor>();
                filesBySymbol = new Dictionary<string, FileDescriptor>();
            }

            internal void AddFile(FileDescriptor fileDescriptor)
            {
                if (filesByName.ContainsKey(fileDescriptor.Name))
                {
                    return;
                }
                filesByName.Add(fileDescriptor.Name, fileDescriptor);

                foreach (var dependency in fileDescriptor.Dependencies)
                {
                    AddFile(dependency);
                }
                foreach (var enumeration in fileDescriptor.EnumTypes)
                {
                    AddEnum(enumeration);
                }
                foreach (var message in fileDescriptor.MessageTypes)
                {
                    AddMessage(message);
                }
                foreach (var service in fileDescriptor.Services)
                {
                    AddService(service);
                }
            }

            private void AddEnum(EnumDescriptor enumDescriptor)
            {
                filesBySymbol[enumDescriptor.FullName] = enumDescriptor.File;
            }

            private void AddMessage(MessageDescriptor messageDescriptor)
            {
                foreach (var nestedEnum in messageDescriptor.EnumTypes)
                {
                    AddEnum(nestedEnum);
                }
                foreach (var nestedType in messageDescriptor.NestedTypes)
                {
                    AddMessage(nestedType);
                }
                filesBySymbol[messageDescriptor.FullName] = messageDescriptor.File;
            }

            private void AddService(ServiceDescriptor serviceDescriptor)
            {
                foreach (var method in serviceDescriptor.Methods)
                {
                    filesBySymbol[method.FullName] = method.File;
                }
                filesBySymbol[serviceDescriptor.FullName] = serviceDescriptor.File;
            }

            internal SymbolRegistry Build()
            {
                return new SymbolRegistry(filesByName, filesBySymbol);
            }
        }
    }
}
