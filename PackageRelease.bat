@echo off
setlocal enabledelayedexpansion

set project_name=Pathwinder
set project_platforms=Win32 x64

set project_has_sdk=no
set project_has_third_party_license=yes

set files_release=LICENSE README.md
set files_release_build_Win32=Pathwinder.HookModule.32.dll
set files_release_build_x64=Pathwinder.HookModule.64.dll

set third_party_license=Hookshot XstdBitSet

call Modules\Infra\Build\Scripts\PackageRelease.bat
