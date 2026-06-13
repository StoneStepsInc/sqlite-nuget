@echo off

setlocal

if "%~1" == "" (
  echo Package revision must be provided as the first argument
  goto :EOF
)

set PKG_VER=3.53.1
set PKG_VER_ABBR=3530200
set PKG_REV=%~1

set SQLITE_FNAME=sqlite-amalgamation-%PKG_VER_ABBR%.zip
set SQLITE_DNAME=sqlite-amalgamation-%PKG_VER_ABBR%
set SQLITE_URL_BASE=https://www.sqlite.org/2026

rem use `openssl dgst -sha3-256/sha256` to verify/convert
rem original SQLite SHA3-256 hash: 81142986038e18f96c4a54e1a72562ae17e502a916f2a7701eff43388cbf1a40
set SQLITE_SHA256=8a310d0a16c7a90cacd4c884e70faa51c902afed2a89f63aaa0126ab83558a32

set SEVENZIP_EXE=c:\Program Files\7-Zip\7z.exe

if NOT EXIST %SQLITE_FNAME% (
  curl --output %SQLITE_FNAME% %SQLITE_URL_BASE%/%SQLITE_FNAME%
)

"%SEVENZIP_EXE%" h -scrcSHA256 %SQLITE_FNAME% | findstr /C:"SHA256 for data" | call devops\check-sha256 "%SQLITE_SHA256%"

if ERRORLEVEL 1 (
  echo SHA-256 signature for %SQLITE_FNAME% does not match
  goto :EOF
)

"%SEVENZIP_EXE%" x %SQLITE_FNAME% 

cd %SQLITE_DNAME%

rem
rem Replace `Community` with `Enterprise` for Enterprise Edition
rem
set VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall

rem
rem Set up environment for the VC++ x64 platform
rem
call "%VCVARSALL%" x64

rem
rem SQLite advises to build a DLL on this page:
rem
rem https://www.sqlite.org/howtocompile.html#building_a_windows_dll
rem
rem , but library functions are not exported, so a DLL without an
rem import library is created, which cannot be used for linking.
rem Static libraries are built instead in both configurations.
rem

rem
rem Feature flags are described on this page:
rem
rem https://sqlite.org/compile.html#_options_to_enable_features_normally_turned_off
rem
rem SQLITE_ENABLE_ICU is not enabled because it would require a
rem fairly complicated setup with ICU DLLs and data files, which
rem is counterproductive to the nature of a simple static library
rem provided by this package. It should be possible, however, to
rem build the ICU extension separately and use it with this static
rem library and the shell.
rem
set SQLITE_FEATURES=/DSQLITE_ENABLE_MATH_FUNCTIONS /DSQLITE_ENABLE_FTS5 /DSQLITE_ENABLE_COLUMN_METADATA /DSQLITE_ENABLE_CARRAY

mkdir Release

cl /c /MD /O2 /Zi %SQLITE_FEATURES% /DNDEBUG /FoRelease\ /FdRelease\sqlite3.pdb sqlite3.c
lib /MACHINE:X64 /OUT:Release\sqlite3.lib Release\sqlite3.obj

cl /MD /O2 /DNDEBUG %SQLITE_FEATURES% /FeRelease\sqlite3.exe shell.c /link Release\sqlite3.obj

mkdir Debug

cl /c /MDd /Od /Zi %SQLITE_FEATURES% /FoDebug\ /FdDebug\sqlite3.pdb sqlite3.c 
lib /MACHINE:X64 /OUT:Debug\sqlite3.lib Debug\sqlite3.obj

rem
rem Create a Nuget package
rem
mkdir ..\nuget\build\native\include
copy /Y *.h ..\nuget\build\native\include\

mkdir ..\nuget\build\native\bin
copy /Y Release\sqlite3.exe ..\nuget\build\native\bin

mkdir ..\nuget\build\native\lib\x64\Release
copy /Y Release\sqlite3.lib ..\nuget\build\native\lib\x64\Release
copy /Y Release\sqlite3.pdb ..\nuget\build\native\lib\x64\Release

mkdir ..\nuget\build\native\lib\x64\Debug
copy /Y Debug\sqlite3.lib ..\nuget\build\native\lib\x64\Debug
copy /Y Debug\sqlite3.pdb ..\nuget\build\native\lib\x64\Debug

cd ..

nuget pack nuget\StoneSteps.SQLite.VS2022.Static.nuspec -Version %PKG_VER%.%PKG_REV%
