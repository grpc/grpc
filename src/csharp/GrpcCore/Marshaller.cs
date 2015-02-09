using System;

namespace Google.GRPC.Core
{
    /// <summary>
    /// For serializing and deserializing messages.
    /// </summary>
    public struct Marshaller<T>
    {
        readonly Func<T,byte[]> serializer;
        readonly Func<byte[],T> deserializer;

        public Marshaller(Func<T, byte[]> serializer, Func<byte[], T> deserializer)
        {
            this.serializer = serializer;
            this.deserializer = deserializer;
        }

        public Func<T, byte[]> Serializer
        {
            get
            {
                return this.serializer;
            }
        }

        public Func<byte[], T> Deserializer
        {
            get
            {
                return this.deserializer;
            }
        }
    }

    public static class Marshallers {

        public static Marshaller<T> Create<T>(Func<T,byte[]> serializer, Func<byte[],T> deserializer)
        {
            return new Marshaller<T>(serializer, deserializer);
        }

        public static Marshaller<string> StringMarshaller
        {
            get
            {
                return new Marshaller<string>(System.Text.Encoding.UTF8.GetBytes, 
                                              System.Text.Encoding.UTF8.GetString);
            }
        }

    }
}

