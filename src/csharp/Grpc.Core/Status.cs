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

using Grpc.Core.Utils;

namespace Grpc.Core
{
    /// <summary>
    /// Represents RPC result, which consists of <see cref="StatusCode"/> and an optional detail string. 
    /// </summary>
    public struct Status
    {
        /// <summary>
        /// Default result of a successful RPC. StatusCode=OK, empty details message.
        /// </summary>
        public static readonly Status DefaultSuccess = new Status(StatusCode.OK, "");

        /// <summary>
        /// Default result of a cancelled RPC. StatusCode=Cancelled, empty details message.
        /// </summary>
        public static readonly Status DefaultCancelled = new Status(StatusCode.Cancelled, "");

        readonly StatusCode statusCode;
        readonly string detail;

        /// <summary>
        /// Creates a new instance of <c>Status</c>.
        /// </summary>
        /// <param name="statusCode">Status code.</param>
        /// <param name="detail">Detail.</param>
        public Status(StatusCode statusCode, string detail)
        {
            this.statusCode = statusCode;
            this.detail = detail;
        }

        /// <summary>
        /// Gets the gRPC status code. OK indicates success, all other values indicate an error.
        /// </summary>
        public StatusCode StatusCode
        {
            get
            {
                return statusCode;
            }
        }

        /// <summary>
        /// Gets the detail.
        /// </summary>
        public string Detail
        {
            get
            {
                return detail;
            }
        }

        /// <summary>
        /// Returns a <see cref="System.String"/> that represents the current <see cref="Grpc.Core.Status"/>.
        /// </summary>
        public override string ToString()
        {
            return string.Format("Status(StatusCode={0}, Detail=\"{1}\")", statusCode, detail);
        }
    }
}
