using Minx.ZMesh;
using static NamedArguments;

var map = GetAs("map", "sysmap.yaml");

if (!TryGetAs("messageBox", out string mbName))
{
    Console.WriteLine("No message box name provided. Exiting.");
    return;
}

using var zmesh = new ZMesh(null, SystemMap.LoadFile(map));

var box = zmesh.At(mbName);

if (GetAs("tell", false))
{
    if (TryGetAs("contentType", out string contentType))
    {
        string content = "";

        TryGetAs("content", out content);

        if (TryGetAs("contentFile", out string contentFile))
        {
            content = File.ReadAllText(contentFile);
        }

        box.Tell(contentType, content);
    }
    else
    {
        Console.WriteLine("No content type or content provided. Exiting.");
    }
}

if (GetAs("ask", false))
{
    if (TryGetAs("contentType", out string contentType))
    {
        string content = "";

        TryGetAs("content", out content);

        if (TryGetAs("contentFile", out string contentFile))
        {
            content = File.ReadAllText(contentFile);
        }

        if (!TryGetAs("timeout", out int timeout))
        {
            timeout = int.MaxValue;
        }

        content = content ?? "{}";

        var result = await box.Ask(contentType, content, TimeSpan.FromMilliseconds(timeout));

        Console.WriteLine(result);
    }
    else
    {
        Console.WriteLine("No content type or content provided. Exiting.");
    }
}