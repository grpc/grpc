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

        /// <summary>
        /// Creates a new instance of <c>Status</c>.
        /// </summary>
        /// <param name="statusCode">Status code.</param>
        /// <param name="detail">Detail.</param>
        public Status(StatusCode statusCode, string detail) : this(statusCode, detail, null)
        {
        }

        /// <summary>
        /// Creates a new instance of <c>Status</c>.
        /// Users should not use this constructor, except for creating instances for testing.
        /// The debug error string should only be populated by gRPC internals.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        /// <param name="statusCode">Status code.</param>
        /// <param name="detail">Detail.</param>
        /// <param name="debugException">Optional internal error details.</param>
        public Status(StatusCode statusCode, string detail, Exception debugException)
        {
            StatusCode = statusCode;
            Detail = detail;
            DebugException = debugException;
        }

        /// <summary>
        /// Gets the gRPC status code. OK indicates success, all other values indicate an error.
        /// </summary>
        public StatusCode StatusCode { get; }

        /// <summary>
        /// Gets the detail.
        /// </summary>
        public string Detail { get; }

        /// <summary>
        /// In case of an error, this field may contain additional error details to help with debugging.
        /// This field will be only populated on a client and its value is generated locally,
        /// based on the internal state of the gRPC client stack (i.e. the value is never sent over the wire).
        /// Note that this field is available only for debugging purposes, the application logic should
        /// never rely on values of this field (it should use <c>StatusCode</c> and <c>Detail</c> instead).
        /// Example: when a client fails to connect to a server, this field may provide additional details
        /// why the connection to the server has failed.
        /// Note: experimental API that can change or be removed without any prior notice.
        /// </summary>
        public Exception DebugException { get; }

        /// <summary>
        /// Returns a <see cref="System.String"/> that represents the current <see cref="Grpc.Core.Status"/>.
        /// </summary>
        public override string ToString()
        {
            if (DebugException != null)
            {
                return $"Status(StatusCode=\"{StatusCode}\", Detail=\"{Detail}\", DebugException=\"{DebugException}\")";
            }
            return $"Status(StatusCode=\"{StatusCode}\", Detail=\"{Detail}\")";
        }
    }
}
