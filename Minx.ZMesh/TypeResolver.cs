using System;
using System.Collections.Concurrent;
using System.Linq;
using System.Reflection;

namespace Minx.ZMesh
{
    public static class TypeResolver
    {
        private static readonly ConcurrentDictionary<string, Type> typeCache = new ConcurrentDictionary<string, Type>();

        public static Type GetTypeInAllAssemblies(string typeName)
        {
            // Find the type in all loaded assemblies
            if (typeCache.TryGetValue(typeName, out Type cachedType))
            {
                return cachedType;
            }

            Type type = Type.GetType(typeName);

            if (type == null)
            {
                foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
                {
                    type = assembly.GetType(typeName);

                    if (type == null)
                    {
                        type = assembly.GetTypes().FirstOrDefault(t => t.Name == typeName);
                    }

                    if (type != null)
                    {
                        break;
                    }
                }
            }

            if (type == null)
            {
                throw new InvalidOperationException($"Could not resolve type. TypeName: {typeName}");
            }

            return type;
        }
    }
}
