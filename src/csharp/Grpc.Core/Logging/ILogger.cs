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

namespace Grpc.Core.Logging
{
    /// <summary>For logging messages.</summary>
    public interface ILogger
    {
        /// <summary>Returns a logger associated with the specified type.</summary>
        ILogger ForType<T>();

        /// <summary>Logs a message with severity Debug.</summary>
        void Debug(string message);

        /// <summary>Logs a formatted message with severity Debug.</summary>
        void Debug(string format, params object[] formatArgs);

        /// <summary>Logs a message with severity Info.</summary>
        void Info(string message);

        /// <summary>Logs a formatted message with severity Info.</summary>
        void Info(string format, params object[] formatArgs);

        /// <summary>Logs a message with severity Warning.</summary>
        void Warning(string message);

        /// <summary>Logs a formatted message with severity Warning.</summary>
        void Warning(string format, params object[] formatArgs);

        /// <summary>Logs a message and an associated exception with severity Warning.</summary>
        void Warning(Exception exception, string message);

        /// <summary>Logs a message with severity Error.</summary>
        void Error(string message);

        /// <summary>Logs a formatted message with severity Error.</summary>
        void Error(string format, params object[] formatArgs);

        /// <summary>Logs a message and an associated exception with severity Error.</summary>
        void Error(Exception exception, string message);
    }
}
