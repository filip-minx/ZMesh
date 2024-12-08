using Newtonsoft.Json;
using System;

namespace Minx.ZMesh.Serialization
{
    public class JsonSerializer : ISerializer
    {
        public T Deserialize<T>(string data)
        {
            return JsonConvert.DeserializeObject<T>(data);
        }

        public object Deserialize(string data, Type type)
        {
            return JsonConvert.DeserializeObject(data, type);
        }

        public string Serialize(object obj)
        {
            return JsonConvert.SerializeObject(obj);
        }
    }
}
