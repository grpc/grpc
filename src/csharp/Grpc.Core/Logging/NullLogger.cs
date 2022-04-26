#region Copyright notice and license

// Copyright 2016 gRPC authors.
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

namespace Grpc.Core.Logging
{
    /// <summary>
    /// Logger which doesn't log any information anywhere.
    /// </summary>
    public sealed class NullLogger : ILogger
    {
        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Debug(string message)
        {
        }

        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Debug(string format, params object[] formatArgs)
        {
        }

        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Error(string message)
        {
        }

        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Error(Exception exception, string message)
        {
        }

        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Error(string format, params object[] formatArgs)
        {
        }

        /// <summary>
        /// Returns a reference to the instance on which the method is called, as
        /// instances aren't associated with specific types.
        /// </summary>
        public ILogger ForType<T>()
        {
            return this;
        }

        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Info(string message)
        {
        }

        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Info(string format, params object[] formatArgs)
        {
        }

        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Warning(string message)
        {
        }

        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Warning(Exception exception, string message)
        {
        }

        /// <summary>
        /// As with all logging calls on this logger, this method is a no-op.
        /// </summary>
        public void Warning(string format, params object[] formatArgs)
        {
        }
    }
}
