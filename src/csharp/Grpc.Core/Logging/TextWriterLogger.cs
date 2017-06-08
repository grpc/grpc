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
using System.IO;
using Grpc.Core.Utils;

namespace Grpc.Core.Logging
{
    /// <summary>Logger that logs to an arbitrary <c>System.IO.TextWriter</c>.</summary>
    public class TextWriterLogger : ILogger
    {
        // Format similar enough to C core log format except nanosecond precision is not supported.
        const string DateTimeFormatString = "MMdd HH:mm:ss.ffffff";

        readonly Func<TextWriter> textWriterProvider;
        readonly Type forType;
        readonly string forTypeString;

        /// <summary>
        /// Creates a console logger not associated to any specific type and writes to given <c>System.IO.TextWriter</c>.
        /// User is responsible for providing an instance of TextWriter that is thread-safe.
        /// </summary>
        public TextWriterLogger(TextWriter textWriter) : this(() => textWriter)
        {
            GrpcPreconditions.CheckNotNull(textWriter);
        }

        /// <summary>
        /// Creates a console logger not associated to any specific type and writes to a <c>System.IO.TextWriter</c> obtained from given provider.
        /// User is responsible for providing an instance of TextWriter that is thread-safe.
        /// </summary>
        public TextWriterLogger(Func<TextWriter> textWriterProvider) : this(textWriterProvider, null)
        {
        }

        /// <summary>Creates a console logger that logs messsage specific for given type.</summary>
        protected TextWriterLogger(Func<TextWriter> textWriterProvider, Type forType)
        {
            this.textWriterProvider = GrpcPreconditions.CheckNotNull(textWriterProvider);
            this.forType = forType;
            if (forType != null)
            {
                var namespaceStr = forType.Namespace ?? "";
                if (namespaceStr.Length > 0)
                {
                     namespaceStr += ".";
                }
                this.forTypeString = namespaceStr + forType.Name + " ";
            }
            else
            {
                this.forTypeString = "";
            }
        }

        /// <summary>
        /// Returns a logger associated with the specified type.
        /// </summary>
        public virtual ILogger ForType<T>()
        {
            if (typeof(T) == forType)
            {
                return this;
            }
            return new TextWriterLogger(this.textWriterProvider, typeof(T));
        }

        /// <summary>Logs a message with severity Debug.</summary>
        public void Debug(string message)
        {
            Log("D", message);
        }

        /// <summary>Logs a formatted message with severity Debug.</summary>
        public void Debug(string format, params object[] formatArgs)
        {
            Debug(string.Format(format, formatArgs));
        }

        /// <summary>Logs a message with severity Info.</summary>
        public void Info(string message)
        {
            Log("I", message);
        }

        /// <summary>Logs a formatted message with severity Info.</summary>
        public void Info(string format, params object[] formatArgs)
        {
            Info(string.Format(format, formatArgs));
        }

        /// <summary>Logs a message with severity Warning.</summary>
        public void Warning(string message)
        {
            Log("W", message);
        }

        /// <summary>Logs a formatted message with severity Warning.</summary>
        public void Warning(string format, params object[] formatArgs)
        {
            Warning(string.Format(format, formatArgs));
        }

        /// <summary>Logs a message and an associated exception with severity Warning.</summary>
        public void Warning(Exception exception, string message)
        {
            Warning(message + " " + exception);
        }

        /// <summary>Logs a message with severity Error.</summary>
        public void Error(string message)
        {
            Log("E", message);
        }

        /// <summary>Logs a formatted message with severity Error.</summary>
        public void Error(string format, params object[] formatArgs)
        {
            Error(string.Format(format, formatArgs));
        }

        /// <summary>Logs a message and an associated exception with severity Error.</summary>
        public void Error(Exception exception, string message)
        {
            Error(message + " " + exception);
        }

        /// <summary>Gets the type associated with this logger.</summary>
        protected Type AssociatedType
        {
            get { return forType; }
        }

        private void Log(string severityString, string message)
        {
            textWriterProvider().WriteLine("{0}{1} {2}{3}",
                severityString,
                DateTime.Now.ToString(DateTimeFormatString, CultureInfo.InvariantCulture),
                forTypeString,
                message);
        }
    }
}
