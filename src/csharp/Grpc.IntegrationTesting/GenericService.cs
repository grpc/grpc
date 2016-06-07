#region Copyright notice and license

// Copyright 2016, Google Inc.
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

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Google.Protobuf;
using Grpc.Core;
using Grpc.Core.Utils;
using NUnit.Framework;
using Grpc.Testing;

namespace Grpc.IntegrationTesting
{
    /// <summary>
    /// Utility methods for defining and calling a service that doesn't use protobufs
    /// for serialization/deserialization.
    /// </summary>
    public static class GenericService
    {
        readonly static Marshaller<byte[]> ByteArrayMarshaller = new Marshaller<byte[]>((b) => b, (b) => b);

        public readonly static Method<byte[], byte[]> StreamingCallMethod = new Method<byte[], byte[]>(
            MethodType.DuplexStreaming,
            "grpc.testing.BenchmarkService",
            "StreamingCall",
            ByteArrayMarshaller,
            ByteArrayMarshaller
        );

        public static ServerServiceDefinition BindHandler(DuplexStreamingServerMethod<byte[], byte[]> handler)
        {
            return ServerServiceDefinition.CreateBuilder()
                .AddMethod(StreamingCallMethod, handler).Build();
        }
    }
}
