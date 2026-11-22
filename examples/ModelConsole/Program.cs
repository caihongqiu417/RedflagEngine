using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Redflag;

// 独立的 .NET 控制台程序，只引用 Redflag-ScriptCore.dll。
class Program
{
    static async Task Ask(ushort port, string prompt, string photo)
    {
        using (var stop = new CancellationTokenSource())
        using (var model = new ModelConnection(port))
        {
            ConsoleCancelEventHandler cancel = (sender, args) => { args.Cancel = true; stop.Cancel(); };
            Console.CancelKeyPress += cancel;
            try
            {
                ModelImage[] images = null;
                if (photo != null)
                {
                    string mime = Path.GetExtension(photo).ToLowerInvariant() == ".png" ? "image/png" : "image/jpeg";
                    images = new[] { new ModelImage("附图", mime, File.ReadAllBytes(photo)) };
                }
                Console.WriteLine(await model.ReplyAsync(prompt, images: images, cancel: stop.Token));
            }
            finally { Console.CancelKeyPress -= cancel; }
        }
    }

    static int Main(string[] args)
    {
        Console.OutputEncoding = new System.Text.UTF8Encoding(false);
        if (args.Length < 1 || args.Length > 3)
        {
            Console.Error.WriteLine("ModelConsole.exe <问题> [本机端口，默认 8716] [PNG 或 JPEG 文件]");
            return 2;
        }
        try {
            Ask(args.Length > 1 ? UInt16.Parse(args[1]) : (ushort)8716, args[0],
                args.Length > 2 ? args[2] : null).GetAwaiter().GetResult();
            return 0;
        } catch (OperationCanceledException) { Console.Error.WriteLine("本轮已取消。"); return 2; }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
    }
}
