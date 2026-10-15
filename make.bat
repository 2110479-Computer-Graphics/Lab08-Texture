@echo off
setlocal enabledelayedexpansion
set BUILD=build
set TARGET=%1

if "%TARGET%"=="" set TARGET=all
if "%TARGET%"=="clean" (
  if exist "%BUILD%" rmdir /s /q "%BUILD%"
  exit /b 0
)

cmake -S . -B %BUILD% -DCMAKE_BUILD_TYPE=Release || exit /b 1

if "%TARGET%"=="all" (
  cmake --build %BUILD% --config Release || exit /b 1
  exit /b 0
)

cmake --build %BUILD% --config Release --target %TARGET% || exit /b 1

set EXE=%BUILD%\%TARGET%.exe
if exist "%BUILD%\Release\%TARGET%.exe" set EXE=%BUILD%\Release\%TARGET%.exe
if not exist "!EXE!" (
  echo could not find !EXE!
  exit /b 1
)
echo running !EXE!
"!EXE!" %2 %3 %4 %5 %6 %7 %8 %9
