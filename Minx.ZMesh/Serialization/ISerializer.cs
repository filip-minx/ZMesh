using System;

namespace Minx.ZMesh.Serialization
{
    public interface ISerializer
    {
        string Serialize(object obj);

        T Deserialize<T>(string data);

        object Deserialize(string data, Type type);
    }
}
