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
