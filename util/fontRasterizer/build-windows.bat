@echo off

set projectName=rasterize-font

set outputDir=build
set srcDir=..
set libDir=..\..\..\lib
set stbLibDir=%libDir%\stb

set debugOptions=/MTd /Ob0 /Od /Zi

set ignoredWarnings=/wd4100 /wd4996

set includeDirs=/I %stbLibDir%
set libraries=user32.lib

mkdir %outputDir% 2> nul

pushd %outputDir% > nul
cl.exe /nologo /W4 /WX %ignoredWarnings% %debugOptions% /Gm- %includeDirs% %srcDir%\main.cpp /Fd%projectName% /Fe%projectName% /link /INCREMENTAL:NO %libraries%

popd

