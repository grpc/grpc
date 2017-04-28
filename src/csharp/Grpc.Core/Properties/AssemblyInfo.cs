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

using System.Reflection;
using System.Runtime.CompilerServices;

[assembly: AssemblyTitle("Grpc.Core")]
[assembly: AssemblyDescription("")]
[assembly: AssemblyConfiguration("")]
[assembly: AssemblyCompany("")]
[assembly: AssemblyProduct("")]
[assembly: AssemblyCopyright("Google Inc.  All rights reserved.")]
[assembly: AssemblyTrademark("")]
[assembly: AssemblyCulture("")]

#if SIGNED
[assembly: InternalsVisibleTo("Grpc.Core.Tests,PublicKey=" +
    "00240000048000009400000006020000002400005253413100040000010001002f5797a92c6fcde81bd4098f43" +
    "0442bb8e12768722de0b0cb1b15e955b32a11352740ee59f2c94c48edc8e177d1052536b8ac651bce11ce5da3a" +
    "27fc95aff3dc604a6971417453f9483c7b5e836756d5b271bf8f2403fe186e31956148c03d804487cf642f8cc0" +
    "71394ee9672dfe5b55ea0f95dfd5a7f77d22c962ccf51320d3")]
[assembly: InternalsVisibleTo("Grpc.Core.Testing,PublicKey=" +
    "00240000048000009400000006020000002400005253413100040000010001002f5797a92c6fcde81bd4098f43" +
    "0442bb8e12768722de0b0cb1b15e955b32a11352740ee59f2c94c48edc8e177d1052536b8ac651bce11ce5da3a" +
    "27fc95aff3dc604a6971417453f9483c7b5e836756d5b271bf8f2403fe186e31956148c03d804487cf642f8cc0" +
    "71394ee9672dfe5b55ea0f95dfd5a7f77d22c962ccf51320d3")]
[assembly: InternalsVisibleTo("Grpc.IntegrationTesting,PublicKey=" +
    "00240000048000009400000006020000002400005253413100040000010001002f5797a92c6fcde81bd4098f43" +
    "0442bb8e12768722de0b0cb1b15e955b32a11352740ee59f2c94c48edc8e177d1052536b8ac651bce11ce5da3a" +
    "27fc95aff3dc604a6971417453f9483c7b5e836756d5b271bf8f2403fe186e31956148c03d804487cf642f8cc0" +
    "71394ee9672dfe5b55ea0f95dfd5a7f77d22c962ccf51320d3")]
#else
[assembly: InternalsVisibleTo("Grpc.Core.Tests")]
[assembly: InternalsVisibleTo("Grpc.Core.Testing")]
[assembly: InternalsVisibleTo("Grpc.IntegrationTesting")]
#endif
