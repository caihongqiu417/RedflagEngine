using System;
using System.IO;
using Redflag;

namespace Workshop
{
    public class SurveyNotes : Entity
    {
        private const string SaveFile = "surveyor.save.yaml";
        private string previousSource;

        void OnCreate()
        {
            if (File.Exists(SaveFile))
            {
                bool loaded = NpcNotes.LoadPreferences(Id, File.ReadAllText(SaveFile));
                Console.WriteLine("Reopened={0} language={1} route={2}", loaded,
                    NpcNotes.GetPreference(Id, "language"), NpcNotes.GetPreference(Id, "route") ?? "unset");
                return;
            }

            // 82 是阀门的实体编号。第二张照片没有对应的实体，以图片编号保存。
            NpcNotes.Report(Id, 82, "玩家说阀门是蓝色的", "round-a.png");
            NpcNotes.Report(Id, 0, "玩家拍下了控制面板", "panel-b.png");
            NpcNotes.SetPreference(Id, "route", "north");
            NpcNotes.SetPreference(Id, "language", "zh");
            Console.WriteLine("Unlisted preference accepted={0}", NpcNotes.SetPreference(Id, "secret", "no"));
            File.WriteAllText(SaveFile, NpcNotes.SavePreferences(Id));
        }

        void OnUpdate(float seconds)
        {
            NpcEvidence note;
            for (int i = 0; NpcNotes.TryRead(Id, i, out note); ++i)
            {
                if (note.Target != 82 || note.Source == EvidenceSource.Reported) continue;
                string source = note.Source.ToString();
                if (source != previousSource)
                    Console.WriteLine("C# {0} {1} {2}", source, note.Target, note.Text);
                previousSource = source;
            }
        }
    }
}
