@echo off
setlocal

:: Usage: dev-build.cmd [Release|Debug]
:: Defaults to Release x64.

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe

echo Building %CONFIG% x64...
"%MSBUILD%" WinDirStat.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m /t:Build /nologo /v:minimal
