using System;
using System.IO;
using System.Net.Http;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Redflag
{
    /* 游戏脚本和创作工具共用这个连接。回复是普通文字，怎样使用由作者决定。 */
    public sealed class ModelConnection : IDisposable
    {
        private readonly HttpClient http;

        public ModelConnection(ushort port = 8716)
        {
            var transport = new HttpClientHandler { UseProxy = false, AllowAutoRedirect = false };
            http = new HttpClient(transport) {
                BaseAddress = new Uri("http://127.0.0.1:" + port + "/v1/"),
                Timeout = TimeSpan.FromSeconds(4)
            };
        }

        /// <summary>发送一轮文字或附图的问题。在脚本的 OnUpdate 中读取完成的任务后，再修改场景。</summary>
        /// <param name="model">空字符串使用本机网关的默认模型。</param>
        public async Task<string> ReplyAsync(string prompt, string instructions = "", ModelImage[] images = null,
            string model = "", CancellationToken cancel = default(CancellationToken))
        {
            cancel.ThrowIfCancellationRequested();
            var input = new Dictionary<string, object> { { "prompt", prompt }, { "instructions", instructions }, { "model", model } };
            if (images != null)
            {
                var attachments = new List<Dictionary<string, string>>();
                foreach (ModelImage image in images) attachments.Add(image.Data);
                input.Add("images", attachments);
            }
            string path = null;
            using (var budget = CancellationTokenSource.CreateLinkedTokenSource(cancel))
            {
                budget.CancelAfter(TimeSpan.FromMinutes(1));
                try
                {
                    // 先取得请求编号。即使玩家在这几毫秒内取消，也能在 finally 里通知服务。
                    Dictionary<string, object> ticket = await Send(HttpMethod.Post, "replies", input, CancellationToken.None).ConfigureAwait(false);
                    Guid id;
                    if (!Guid.TryParseExact((string)ticket["id"], "N", out id)) throw new InvalidDataException("invalid_service_reply");
                    path = "replies/" + id.ToString("N");

                    for (;;)
                    {
                        await Task.Delay(40, budget.Token).ConfigureAwait(false);
                        Dictionary<string, object> result = await Send(HttpMethod.Get, path, null, budget.Token).ConfigureAwait(false);
                        string status = (string)result["status"];
                        if (status == "pending") continue;
                        if (status == "completed") return (string)result["text"];
                        throw new InvalidOperationException((string)result["error"]);
                    }
                }
                catch (OperationCanceledException error) when (!cancel.IsCancellationRequested)
                {
                    throw new TimeoutException("model_timeout", error);
                }
                finally
                {
                    if (path != null)
                    {
                        try { await Send(HttpMethod.Delete, path, null, CancellationToken.None).ConfigureAwait(false); }
                        catch (HttpRequestException) { /* 服务掉线时，请求会随本机队列一起清理。 */ }
                        catch (OperationCanceledException) { }
                        catch (ObjectDisposedException) { }
                        catch (InvalidOperationException) { /* 服务重启后不会保留旧编号。 */ }
                    }
                }
            }
        }

        public void Dispose() { http.Dispose(); }

        // ---- 本机服务的 JSON 请求与返回值 ----

        private async Task<Dictionary<string, object>> Send(HttpMethod method, string path, Dictionary<string, object> input, CancellationToken cancel)
        {
            using (var request = new HttpRequestMessage(method, path))
            {
                var json = new JavaScriptSerializer { MaxJsonLength = 12 * 1024 * 1024 };
                if (input != null)
                    request.Content = new StringContent(json.Serialize(input), Encoding.UTF8, "application/json");
                using (HttpResponseMessage response = await http.SendAsync(request, cancel).ConfigureAwait(false))
                {
                    string body = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                    Dictionary<string, object> reply = json.Deserialize<Dictionary<string, object>>(body);
                    if (!response.IsSuccessStatusCode) throw new InvalidOperationException((string)reply["error"]);
                    return reply;
                }
            }
        }
    }
}
