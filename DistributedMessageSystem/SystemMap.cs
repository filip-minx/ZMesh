using YamlDotNet.RepresentationModel;

namespace DistributedMessanger
{
    public class SystemMap
    {
        public static Dictionary<string, string> LoadFile(string path)
        {
            var content = File.ReadAllText(path);

            var input = new StringReader(content);
            var yaml = new YamlStream();
            yaml.Load(input);

            // Get the root mapping
            var mapping = (YamlMappingNode)yaml.Documents[0].RootNode;

            // Get the messageBoxes sequence
            var messageBoxes = (YamlSequenceNode)mapping.Children[new YamlScalarNode("systemMap")];

            // Create the dictionary
            var result = new Dictionary<string, string>();

            foreach (var box in messageBoxes)
            {
                string[] parts = box.ToString().Split('/');
                string name = parts[1];
                string address = parts[0];
                result[name] = address;
            }

            return result;
        }
    }
}
