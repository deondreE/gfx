require "export-compile-commands"

workspace "GFX"
    architecture "x86_64"
    configurations { "Debug", "Release" }
    startproject "GraphicsTest"

    outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

    filter "system:windows"
        buildoptions { "/MP" }
    filter {}

vulkan_sdk = os.getenv("VULKAN_SDK")

project "GraphicsLIB"
    location "GraphicsLIB"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "Off"
    pchheader "gfxpch.hpp"
    pchsource "gfx/gfxpch.cpp"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "gfx/**.hpp",
        "gfx/**.cpp",
        "gfx/**.mm",
		"gfx/vendor/**.h" 
    }

    includedirs {
        "gfx",
		"include"
    }

    if vulkan_sdk then
        includedirs { "$(VULKAN_SDK)/Include" }
        filter "system:windows"
            libdirs { "$(VULKAN_SDK)/Lib" }
            links { "vulkan-1" }
        filter "system:macosx"
        filter "system:linux"
            links { "vulkan" }
        filter {}
    end

    filter "system:windows"
        systemversion "latest"
		characterset "Unicode"

        defines {
            "WIN32_LEAN_AND_MEAN",
			"WIN32",
            "NOMINMAX",
            "VK_USE_PLATFORM_WIN32_KHR"
        }

        links {
            "d3d12",
            "dxgi",
            "d3dcompiler",
            "dxguid"
        }

    filter "system:linux"
        defines { "GFX_PLATFORM_LINUX", "VK_USE_PLATFORM_XLIB_KHR" }
        links { "X11", "vulkan" }

    filter "system:macosx"
        linkoptions { "-framework Metal", "-framework MetalKit", "-framework Cocoa" }
    filter {}

    filter "configurations:Debug"
        defines { "_DEBUG" }
        symbols "On"
        runtime "Debug"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
        runtime "Release"

    filter {}

project "GraphicsTest"
    location "GraphicsTest"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "Off"

    targetdir ("bin/" .. outputdir .. "/%{prj.name}")
    objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "sandbox/**.hpp",
        "sandbox/**.cpp",
    }

    includedirs {
        "gfx/include",
        "sandbox"
    }

    links {
        "GraphicsLIB"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"
        runtime "Debug"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
        runtime "Release"

    filter {}
