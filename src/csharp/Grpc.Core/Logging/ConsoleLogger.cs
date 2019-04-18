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
using System.Globalization;

namespace Grpc.Core.Logging
{
    /// <summary>Logger that logs to System.Console.</summary>
    public class ConsoleLogger : TextWriterLogger
    {
        /// <summary>Creates a console logger not associated to any specific type.</summary>
        public ConsoleLogger() : this(null)
        {
        }

        /// <summary>Creates a console logger that logs messsage specific for given type.</summary>
        private ConsoleLogger(Type forType) : base(() => Console.Error, forType)
        {
        }
 
        /// <summary>
        /// Returns a logger associated with the specified type.
        /// </summary>
        public override ILogger ForType<T>()
        {
            if (typeof(T) == AssociatedType)
            {
                return this;
            }
            return new ConsoleLogger(typeof(T));
        }
    }
}
