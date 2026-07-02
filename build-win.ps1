Function Info($msg) {
  Write-Host -ForegroundColor DarkGreen "`nINFO: $msg`n"
}

Function CheckReturnCodeOfPreviousCommand($msg) {
  if(-Not $?) {
    Write-Error "${msg}. Error code: $LastExitCode"
    exit 1
  }
}

Function GetVersion() {
  try { $tag = git describe --exact-match --tags HEAD 2>$null } catch { }
  if(-Not $?) { $tag = "v0.0" }
  $commitHash = git rev-parse --short HEAD
  CheckReturnCodeOfPreviousCommand "Failed to get git commit hash"
  return "$($tag.Substring(1))~$commitHash"
}

Function BuildAndTest($buildType, $arch) {
  Info "Build and test $buildType-$arch"

  $thisBuildDir = "$buildDir/win-$buildType-$arch"

  Info "Cmake generate cache"
  cmake -S $root `
        -B $thisBuildDir `
        -G Ninja `
        -D CMAKE_BUILD_TYPE=$buildType `
        -D CONSOLE_APP_VERSION=$version
  CheckReturnCodeOfPreviousCommand "cmake cache failed"

  Info "Cmake build"
  cmake --build $thisBuildDir
  CheckReturnCodeOfPreviousCommand "cmake build failed"

  Info "Run tests"
  ctest --test-dir $thisBuildDir
  CheckReturnCodeOfPreviousCommand "tests failed"
}

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$version = GetVersion
$root = Resolve-Path $PSScriptRoot
$buildDir = "$root/build"

Info "Version '$version'"

Info "Find Visual Studio installation path"
$vswhereCommand = Get-Command -Name "${Env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$installationPath = & $vswhereCommand -prerelease -latest -property installationPath

Info "Open Visual Studio Developer PowerShell amd64"
& "$installationPath/Common7/Tools/Launch-VsDevShell.ps1" -SkipAutomaticLocation -Arch amd64

BuildAndTest -buildType Debug -arch amd64
BuildAndTest -buildType Release -arch amd64

Info "Open Visual Studio Developer PowerShell x86"
& "$installationPath/Common7/Tools/Launch-VsDevShell.ps1" -SkipAutomaticLocation -Arch x86 -HostArch amd64

BuildAndTest -buildType Debug -arch x86
BuildAndTest -buildType Release -arch x86

Info "Create Windows x64 zip archive"
Compress-Archive -Path "$buildDir/win-Release-amd64/TrId2Console.exe" -DestinationPath "$buildDir/TrId2Console-windows-x64.zip" -Force
New-Item $buildDir/TrId2Lib -Force -ItemType "directory" > $null
Copy-Item -Path $root/src/TrId2Lib/TrId2FileTypeDetector.h -Destination $buildDir/TrId2Lib -Force
Copy-Item -Path $buildDir/win-Release-amd64/Gen/TrId2Database.h -Destination $buildDir/TrId2Lib -Force
Compress-Archive -Path $buildDir/TrId2Lib -DestinationPath $buildDir/TrId2Lib.zip -Force
