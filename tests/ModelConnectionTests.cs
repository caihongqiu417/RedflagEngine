using System;
using System.IO;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using Redflag;

class ModelConnectionTests
{
    static async Task Run(ushort port, string image)
    {
        using (var model = new ModelConnection(port))
        {
            string first = await model.ReplyAsync("请把「钥匙可以打开储物柜」改写成一句中文物品说明，保留钥匙和储物柜两个词。");
            Console.WriteLine(first);
            if (!first.Contains("钥匙") || !first.Contains("储物柜")) throw new Exception("invalid item description");

            if (!String.IsNullOrEmpty(image))
            {
                var frame = new ModelImage("维修间", "image/png", File.ReadAllBytes(image));
                string second = await model.ReplyAsync("图中小箱子是什么颜色？只写一种中文颜色。", images: new[] { frame });
                Console.WriteLine(second);
                if (!second.Contains("红")) throw new Exception("invalid image description");
            }
            try { await model.ReplyAsync("test", model: "not-configured"); throw new Exception("invalid model accepted"); }
            catch (InvalidOperationException ex) { if (ex.Message != "model_unavailable") throw; }
            using (var cancelled = new CancellationTokenSource())
            {
                var pending = model.ReplyAsync("用中文写一段介绍钥匙的文字。", cancel: cancelled.Token);
                cancelled.Cancel();
                try { await pending; throw new Exception("cancel did not reach caller"); }
                catch (OperationCanceledException) { }
            }
            // 在同一连接上重新发送，确认上一轮取消不妨碍新请求。
            string next = await model.ReplyAsync("请只回答汉字「木」。");
            Console.WriteLine(next);
            if (!next.Contains("木")) throw new Exception("next reply failed");
        }
    }

    static int Main(string[] args)
    {
        Console.OutputEncoding = new System.Text.UTF8Encoding(false);
        if (args.Length == 2 && args[1] == "--offline")
        {
            using (var model = new ModelConnection(UInt16.Parse(args[0])))
            {
                try { model.ReplyAsync("测试断线").GetAwaiter().GetResult(); return 1; }
                catch (HttpRequestException) { return 0; }
            }
        }
        try { Run(UInt16.Parse(args[0]), args.Length > 1 ? args[1] : null).GetAwaiter().GetResult(); return 0; }
        catch (Exception e) { Console.Error.WriteLine(e.Message); return 1; }
    }
}
