namespace Redflag
{
    public static class Playtest
    {
        /// <summary>把本帧发生的游戏事件写入测试记录。entity 使用场景中的稳定编号，0 表示整局。</summary>
        public static void Record(ulong entity, string kind, string text)
        {
            InternalCalls.Playtest_Record(entity, kind, text);
        }
    }
}
