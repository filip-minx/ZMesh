using System.Text.RegularExpressions;

public static class NamedArguments
{
    private static readonly Dictionary<string, string> arguments = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

    static NamedArguments()
    {
        ParseArguments();
    }

    private static void ParseArguments()
    {
        var namedArgumentRegex = new Regex(@"^\s*--\s*.+");

        var args = Environment.GetCommandLineArgs();

        string name, value;

        for (int i = 0; i < args.Length; i++)
        {
            name = args[i];

            if (namedArgumentRegex.IsMatch(name))
            {
                name = name.Substring(name.IndexOf("--") + 2);

                if ((i < args.Length - 1) && (!namedArgumentRegex.IsMatch(args[i + 1])))
                {
                    value = args[i + 1];
                    i++;
                }
                else
                {
                    value = "true";
                }

                arguments[name] = value;
            }
        }
    }

    public static bool TryGetAs<T>(string argName, out T value)
    {
        if (!arguments.TryGetValue(argName, out var strValue))
        {
            value = default;

            return false;
        }

        value = (T)Convert.ChangeType(strValue, typeof(T));

        return true;
    }

    public static T GetAs<T>(string argName, T defaultValue)
    {
        if (!TryGetAs(argName, out T value))
        {
            return defaultValue;
        }

        return value;
    }
}
