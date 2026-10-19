# TrId2-file-type-identifier

C++ library and Console utility to detect a file type based on its content.<br>
This tool is similar to [TrID](https://mark0.net/soft-trid-e.html) and [UNIX File](https://en.wikipedia.org/wiki/File_(command)).

## Features
* Database of ~22000 file types
* Very fast
* Supports Windows, Linux, macOS
* The C++ library is header only. Depends only on [simdjson](https://github.com/simdjson/simdjson)

## How it works
The tool reads up to 10MB of the file, goes through its database, and finds the most likely file-type candidates.

## How to use the console tool
* Download and extract the archive from the latest [release](https://github.com/Papyrus7914/TrId2-file-type-identifier/releases)
  * `TrId2Console-windows-x64` - portable Windows x64 executable (requires Windows 10 or higher)
  * `TrId2Console-linux-x64` - portable Linux x64 executable
  * `TrId2Console-macos-arm64` - portable macOS arm64 executable
* Keep the included `TrId2Database.json` next to the executable or use `--database-file-path /path/to/TrId2Database.json` to specify the database.
* Usage examples (use `--help` parameter to get the usage information):
```
> TrId2Console C:\Windows\System32\ntdll.dll
 33.2% (exe) Win64 Executable (generic)
    Url: https://en.wikipedia.org/wiki/Portable_Executable
    MimeType: application/octet-stream
 25.6% (exe) Win16 NE executable (generic)
    Url: https://en.wikipedia.org/wiki/Windows_3.0
    MimeType: application/octet-stream
 10.5% (icl) Windows Icons Library (generic)
    Url: http://fileformats.archiveteam.org/wiki/Icon_library
    MimeType: image/x-ms-icl
 10.3% (exe) OS/2 Executable (generic)
    Url: https://en.wikipedia.org/wiki/OS/2
    MimeType: application/octet-stream
 10.2% (exe) Generic Win/DOS Executable
    MimeType: application/octet-strea
```
```
> TrId2Console.exe --json C:\Windows\System32\ntdll.dll
[
  {
    "Description": "",
    "FileNameExtensions": [
      "exe"
    ],
    "MimeType": "application/octet-stream",
    "Name": "Win64 Executable (generic)",
    "Probability": 33.19083969465649,
    "Url": "https://en.wikipedia.org/wiki/Portable_Executable"
  },
...
```

## How to use the C++ library
* Copy [TrId2FileTypeDetector.h](https://github.com/Papyrus7914/TrId2-file-type-identifier/blob/main/src/TrId2Lib/TrId2FileTypeDetector.h) to your project
* Get `TrId2Database.json` file from the `TrId2Console-windows-x64` archive from the [Releases](https://github.com/Papyrus7914/TrId2-file-type-identifier/releases) page
* Look at [example.cpp](https://github.com/Papyrus7914/TrId2-file-type-identifier/blob/main/src/Tests/example.cpp) for how to use the library

## How to work with the codebase
* Required: Python with [PyYAML](https://pypi.org/project/PyYAML/)
* Open the repository with any IDE that supports CMake. No extra configuration steps are required.