#pragma once

namespace Redflag
{
    struct FogSettings
    {
        float Density = .075f;  // 光穿过 d 个场景单位后，还剩原亮度的 exp(-Density × d)。
        float Albedo = .85f;    // 光穿过雾时会减弱。取 0.85 表示减弱的这部分光有 85% 向其他方向散射，15% 被吸收。
        float Anisotropy = .3f; // 0 表示各个方向一样亮。取正值时，迎着灯看雾更亮。
        float Distance = 30.f;  // 雾沿视线计算的最远距离，单位同场景。先遇到模型就算到模型表面。
        int Steps = 12;         // 默认把视线穿过的雾分成 12 段，各在中点算一次光照。段数越多，沿途取样越密。
    };
}
