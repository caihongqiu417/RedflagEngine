#pragma once

#include "Redflag/Animation/AnimationTimeline.h"

static void CheckAnimationEvents()
{
    AnimationState loop;
    loop.ID = "land";
    loop.Events = {{"recover", 0.f, .25f}, {"knee", .125f, {}}, {"finish", .5f, {}}};
    loop.HasContact = true;
    loop.Contact = .75f;

    AnimationTimeline actor, partner;
    Check(actor.Play(loop, .5f) && partner.Play(loop, .5f), "two actors start the same clip");
    Check(actor.IsOpen("recover") && partner.IsOpen("recover"), "a window starting at zero opens immediately");
    actor.Advance(1.125f);
    AnimationNotice notice;
    std::vector<std::string> events;
    while (actor.Read(notice))
        events.push_back(notice.Name + (notice.Phase == NotifyPhase::End ? "-" : notice.Phase == NotifyPhase::Begin ? "+" : ""));
    const std::vector<std::string> expected = {"recover+","knee","recover-","contact","finish",
        "recover+","knee","recover-","contact","finish","recover+","knee"};
    Check(events == expected, "one long frame delivers every marker across two loops, including the last and first instants");
    Check(partner.Time() == 0 && partner.IsOpen("recover"), "one actor's progress and consumed events do not advance the other");

    actor.Cancel();
    actor.Cancel();
    Check(actor.Read(notice) && notice.Interrupted && notice.Seconds == .125f && notice.Phase == NotifyPhase::End
        && !actor.Read(notice) && !actor.IsOpen("recover"), "cancellation closes an open window exactly once");

    loop.ID = "once";
    loop.Loop = false;
    Check(partner.Play(loop, .5f), "switch to a non-looping state");
    Check(partner.Read(notice) && notice.Phase == NotifyPhase::Begin && notice.State == "land", "preserve unread Begin");
    Check(partner.Read(notice) && notice.Interrupted && notice.State == "land", "interrupt the previous state before starting the next");
    Check(partner.Read(notice) && notice.Phase == NotifyPhase::Begin && notice.State == "once", "next state starts after interrupted End");
    partner.Advance(.25f);
    Check(!partner.IsOpen("recover") && partner.Read(notice) && notice.Name == "knee" &&
        partner.Read(notice) && !notice.Interrupted && notice.Seconds == .25f, "natural End is delivered on the exact endpoint");
    partner.Advance(2.f);
    partner.Advance(.5f);
    Check(!partner.Playing() && partner.Read(notice) && notice.Name=="contact" && partner.Read(notice)
        && notice.Name=="finish" && !partner.Read(notice), "completed clip holds its final pose and does not repeat notices");
    Check(!partner.Play(loop, .4f), "a marker beyond the actual model clip is rejected");
}
