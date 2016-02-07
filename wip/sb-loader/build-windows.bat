@echo off

set projectName=sb-load

set outputDir=build
set srcDir=..

set debugOptions=/MTd /Ob0 /Od /Zi
set releaseOptions=/MT /O2 /Oi

set ignoredWarnings=/wd4100 /wd4996

set libraries=User32.lib

mkdir %outputDir% 2> nul

pushd %outputDir% > nul
cl.exe /nologo /W4 /WX %ignoredWarnings% %debugOptions% /Gm- %srcDir%\main.cpp /Fd%projectName% /Fe%projectName% /INCREMENTAL:NO %libraries%
popd

