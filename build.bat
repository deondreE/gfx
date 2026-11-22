@echo off
cd /d "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

if "%1"=="" (
	echo Usage: build.bat [Debug/Release]
	exit /b 1
)

msbuild GFX.sln /p:Configuration=%1 /p:Platform=x64 /p:UseEnv=true
