@echo off

set projectName=gen-fcn-ptrs

set outputDir=build
set srcDir=..

set debugOptions=/MTd /Ob0 /Od /Zi

set ignoredWarnings=/wd4996

set libraries=User32.lib

mkdir %outputDir% 2> nul

pushd %outputDir% > nul
cl.exe /nologo /W4 /WX %ignoredWarnings% %debugOptions% /Gm- %srcDir%\main.cpp /Fd%projectName% /Fe%projectName% /INCREMENTAL:NO %libraries%

set buildResult=%ERRORLEVEL%
popd

IF %buildResult% GEQ 1 EXIT /B 1
mkdir ..\..\src\generated 2> nul
build\%projectName%.exe functionNames.txt ..\..\src\generated\glFunctions.cpp

