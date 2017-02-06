#region Copyright notice and license
// Copyright 2015, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
