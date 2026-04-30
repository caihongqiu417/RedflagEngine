project "SandboxProject"
    kind "SharedLib"
    language "C#"
    dotnetframework "4.7.2"

    targetname "Sandbox"
    targetdir ("%{wks.location}/RedflagForge/Resources/Scripts")
    objdir ("%{wks.location}/RedflagForge/Resources/Scripts/Intermediates/SandboxProject")

    files {
        "Source/**.cs",
        "Properties/**.cs"
    }

    links {
        "Redflag-ScriptCore"
    }

    filter "configurations:Debug"
        optimize "Off"
        symbols "On"

    filter "configurations:Release"
        optimize "On"
        symbols "Default"

    filter "configurations:Dist"
        optimize "Full"
        symbols "Off"
