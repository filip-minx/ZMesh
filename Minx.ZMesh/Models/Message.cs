using Newtonsoft.Json;
using Newtonsoft.Json.Converters;

namespace Minx.ZMesh.Models
{
    public abstract class Message
    {
        [JsonConverter(typeof(StringEnumConverter))]
        public abstract MessageType MessageType { get; }
        public string ContentType { get; set; }
        public string Content { get; set; }
        public string MessageBoxName { get; set; }
    }
}
