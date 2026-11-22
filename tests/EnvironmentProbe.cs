using System;
using Redflag;

namespace Workshop
{
    public class EnvironmentProbe : Entity
    {
        private string previous;

        void OnUpdate(float seconds)
        {
            EnvironmentFact[] facts = NpcSense.GetEnvironment(Id);
            EnvironmentFact lamp = Array.Find(facts, fact => fact.Id == "zone/90/light/91");
            string current = facts.Length + " " + (lamp.Description ?? "no-lamp");
            if (current != previous) Console.WriteLine("ZONE " + current);
            previous = current;
        }
    }
}
