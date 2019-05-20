-- _ACTION is a premake global variable and for our usage will be vs2012, vs2013, etc.
-- Strip "vs" from this string to make a suffix for solution and project files.
_SUFFIX = _ACTION

workspace "D3D12Sample"
configurations { "Debug", "Release" }
platforms { "x64" }
location "../build"
filename ("D3D12Sample_" .. _SUFFIX)
startproject "D3D12Sample"

filter "platforms:x64"
system "Windows"
architecture "x64"
includedirs { }
libdirs { }


project "D3D12Sample"
kind "ConsoleApp"
language "C++"
location "../build"
filename ("D3D12Sample_" .. _SUFFIX)
targetdir "../bin"
objdir "../build/Desktop_%{_SUFFIX}/%{cfg.platform}/%{cfg.buildcfg}"
floatingpoint "Fast"
files { "../src/*.h", "../src/*.cpp" }
flags { "NoPCH", "FatalWarnings" }
characterset "Unicode"

filter "configurations:Debug"
defines { "_DEBUG", "DEBUG" }
flags { }
targetsuffix ("_Debug_" .. _SUFFIX)

filter "configurations:Release"
defines { "NDEBUG" }
optimize "On"
flags { "LinkTimeOptimization" }
targetsuffix ("_Release_" .. _SUFFIX)

filter { "platforms:x64" }
defines { "WIN32", "_CONSOLE", "PROFILE", "_WINDOWS", "_WIN32_WINNT=0x0601" }
links { "d3d12.lib", "dxgi.lib" }

filter { "configurations:Debug", "platforms:x64" }
buildoptions { "/MDd" }

filter { "configurations:Release", "platforms:Windows-x64" }
buildoptions { "/MD" }
