using Minx.ZMesh;
using Newtonsoft.Json;
using static NamedArguments;

var map = GetAs("map", "sysmap.yaml");
var sysmap = SystemMap.LoadFile(map);

if (TryGetAs("listen", out string messageBoxNames))
{
    List<IZMesh> meshes = new List<IZMesh>();
    object listenLock = new object();

    foreach (var name in messageBoxNames.Split(','))
    {
        var messageBoxName = name.Trim();

        if (string.IsNullOrEmpty(messageBoxName))
        {
            Console.WriteLine("Message box name is empty. Exiting.");
            return;
        }

        Console.WriteLine($"Listening to {messageBoxName} on {sysmap[messageBoxName]}");
        new ZMesh(sysmap[messageBoxName], sysmap)
            .At(messageBoxName).TellReceived += (sender, args) =>
            {
                var messageBox = (IAbstractMessageBox)sender;

                messageBox.TryListen(args.ContentType, (content) =>
                {
                    lock (listenLock)
                    {
                        var currentColor = Console.ForegroundColor;
                        Console.ForegroundColor = ConsoleColor.Yellow;
                        Console.Write(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + " ");
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.Write(messageBoxName);
                        Console.ForegroundColor = currentColor;
                        Console.Write(" <= ");
                        Console.ForegroundColor = ConsoleColor.Cyan;
                        Console.WriteLine($"[{args.ContentType}]");
                        Console.ForegroundColor = currentColor;
                        Console.WriteLine(JsonConvert.SerializeObject(JsonConvert.DeserializeObject(content), Formatting.Indented));
                    }
                });
            };
    };

    return;
}

if (!TryGetAs("messageBox", out string mbName))
{
    Console.WriteLine("No message box name provided. Exiting.");
    return;
}

using var zmesh = new ZMesh(null, sysmap);
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