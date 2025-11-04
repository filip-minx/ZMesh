// See https://aka.ms/new-console-template for more information
using Minx.ZMesh;

Console.WriteLine("Hello, World!");

var systemMap = new Dictionary<string, string>()
{
    { "BoxA", "127.0.0.1:5000" },
    { "BoxB", "127.0.0.1:5001" }
};

var zmeshNodeA = new ZMesh("127.0.0.1:5000", systemMap);
var zmeshNodeB = new ZMesh("127.0.0.1:5001", systemMap);

// Typically these nodes would exist one per process.
// All of the processes share the message boxes.
// Each box is owned by exactly one process and only that process can read messages from that box -> can listen to `Tell` messages and answer `Question` messages.
// Any node can send messages (Tell and Question) to any box.

_ = Task.Run(() =>
{
    while (true)
    {
        var boxA = zmeshNodeA.At("BoxA");
        var boxB = zmeshNodeA.At("BoxB");

        boxA.TryListen("HelloMsg", (content) =>
        {
            Console.WriteLine($"BoxA received Hello with content: {content}");
        });

        boxB.Tell("HelloMsg", "Greetings from node A");

        boxB.Ask("WhatIsYourName", "Node A is asking").ContinueWith(answerTask =>
        {
            var answer = answerTask.Result;
            Console.WriteLine($"Node A received answer from BoxB: {answer}");
        });

        Thread.Sleep(1000);
    }
});

_ = Task.Run(() =>
{
    while (true)
    {
        var boxA = zmeshNodeB.At("BoxA");
        var boxB = zmeshNodeB.At("BoxB");

        boxB.TryListen("HelloMsg", (content) =>
        {
            Console.WriteLine($"BoxB received Hello with content: {content}");
        });

        boxA.Tell("HelloMsg", "Greetings from node B");

        boxB.TryAnswer("WhatIsYourName", (questionContent) =>
        {
            Console.WriteLine($"BoxB received question: {questionContent}");

            return new Answer()
            {
                ContentType = "NameAnswer",
                Content = "I am BoxB"
            };
        });

        Thread.Sleep(1000);
    }
});