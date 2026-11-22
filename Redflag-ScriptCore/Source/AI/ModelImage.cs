using System;
using System.Collections.Generic;

namespace Redflag
{
    public sealed class ModelImage
    {
        internal Dictionary<string, string> Data { get; private set; }

        /// <summary>bytes 是整张 PNG 或 JPEG 文件的内容。构造完成后，调用者可以释放原数组。</summary>
        public ModelImage(string name, string mime, byte[] bytes)
        {
            Data = new Dictionary<string, string> { { "id", name }, { "mime", mime }, { "data", Convert.ToBase64String(bytes) } };
        }
    }
}
