using System;

namespace Grpc.Core.Logging
{
    internal class NullLogger : ILogger
    {
        public ILogger ForType<T>()
        {
            return this;
        }

        public void Debug(string message)
        {
        }

        public void Debug(string format, params object[] formatArgs)
        {
        }

        public void Info(string message)
        {
        }

        public void Info(string format, params object[] formatArgs)
        {
        }

        public void Warning(string message)
        {
        }

        public void Warning(string format, params object[] formatArgs)
        {
        }

        public void Warning(Exception exception, string message)
        {
        }

        public void Error(string message)
        {
        }

        public void Error(string format, params object[] formatArgs)
        {
        }

        public void Error(Exception exception, string message)
        {
        }
    }
}
