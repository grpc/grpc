using System;

namespace Google.GRPC.Core
{
    /// <summary>
    /// For serializing and deserializing messages.
    /// </summary>
    public interface IMarshaller<T>
    {
        byte[] Serialize(T value);

        T Deserialize(byte[] payload);
    }

    /// <summary>
    /// UTF-8 Marshalling for string. Useful for testing.
    /// </summary>
    internal class StringMarshaller : IMarshaller<string> {

        public byte[] Serialize(string value)
        {
            return System.Text.Encoding.UTF8.GetBytes(value);
        }

        public string Deserialize(byte[] payload)
        {
            return System.Text.Encoding.UTF8.GetString(payload);
        }
    }
}

