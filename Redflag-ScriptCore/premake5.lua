project "Redflag-ScriptCore"
    kind "SharedLib"
    language "C#"
    dotnetframework "4.7.2"
    links { "System", "System.Net.Http", "System.Web.Extensions" }

    targetdir ("%{wks.location}/RedflagForge/Resources/Scripts")
    objdir ("%{wks.location}/RedflagForge/Resources/Scripts/Intermediates")

    files {
        "Source/**.cs",
        "Properties/**.cs"
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
