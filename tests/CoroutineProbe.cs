using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Redflag;

namespace Workshop
{
    public class CoroutineProbe : Entity
    {
        private int frame;
        private readonly GameSignal door = new GameSignal();
        private Coroutine abandoned, fault, self;
        private readonly TaskCompletionSource<int> delivery = new TaskCompletionSource<int>();

        private void Write(string step)
        {
            File.AppendAllText("routine-events.txt", string.Format("{0} {1} {2} {3}\n", Id, frame, Thread.CurrentThread.ManagedThreadId, step));
        }

        void OnCreate()
        {
            Write("created");
            door.Raise();
            StartCoroutine(MainSequence());
            abandoned = StartCoroutine(Hold());
            fault = StartCoroutine(Fail());
            self = StartCoroutine(StopSelf());
        }

        IEnumerator<GameWait> MainSequence()
        {
            Write("start");
            yield return null;
            Write("next-frame");
            yield return GameWait.Seconds(.5f);
            Write("half-second");
            yield return door.Next();
            Write("door-open");
            yield return GameWait.For(delivery.Task);
            Write("received-" + delivery.Task.Result);
            var cancelled = new TaskCompletionSource<int>();
            cancelled.SetCanceled();
            yield return GameWait.For(cancelled.Task);
            if (cancelled.Task.IsCanceled) Write("task-cancelled");
            var failed = new TaskCompletionSource<int>();
            failed.SetException(new IOException("offline"));
            yield return GameWait.For(failed.Task);
            Write("task-error-" + failed.Task.Exception.InnerException.Message);
            TextAssetRead file = new TextAssetRead("lesson.txt");
            yield return file;
            Write("text-" + file.Text);
            TextAssetRead missing = new TextAssetRead("missing.txt");
            yield return missing;
            Write(missing.Error);
            GetComponent<TransformComponent>().Translation = new Vector3(2, 3, 5);
        }

        IEnumerator<GameWait> Hold()
        {
            try { yield return GameWait.Seconds(999); Write("unexpected"); }
            finally { Write("cancel-finally"); }
        }

        IEnumerator<GameWait> StopSelf()
        {
            try { self.Cancel(); yield return null; Write("unexpected"); }
            finally { Write("self-finally"); }
        }

        IEnumerator<GameWait> Fail()
        {
            yield return null;
            throw new InvalidOperationException("probe controlled failure");
        }

        void OnUpdate(float dt)
        {
            ++frame;
            if (frame == 2) abandoned.Cancel();
            if (frame == 4 && fault.Error != null && !fault.IsCancelled) Write("isolated-failure");
            if (frame == 6) door.Raise();
            if (frame == 8) delivery.SetResult(17);
        }
    }

    // 这个实体没有 OnUpdate。删除它时，尚在读取或等待的协程都应运行迭代器的 finally。
    public class Unfinished : Entity
    {
        void OnCreate() { StartCoroutine(Pending()); }

        IEnumerator<GameWait> Pending()
        {
            try
            {
                if (Id == 2) yield return new TextAssetRead("lesson.txt");
                else yield return new GameSignal().Next();
                File.AppendAllText("routine-events.txt", "unexpected\n");
            }
            finally { File.AppendAllText("routine-events.txt", "disposed-" + Id + "\n"); }
        }
    }
}
