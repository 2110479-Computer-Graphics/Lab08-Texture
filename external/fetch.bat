@echo off
setlocal
set COMMIT=f0569113c93ad095470c54bf34a17b36646bbbb5
set BASE=https://raw.githubusercontent.com/nothings/stb/%COMMIT%
set DIR=%~dp0stb
if not exist "%DIR%" mkdir "%DIR%"
curl -fL -o "%DIR%\stb_image.h"       "%BASE%/stb_image.h"       || exit /b 1
curl -fL -o "%DIR%\stb_image_write.h" "%BASE%/stb_image_write.h" || exit /b 1
