@echo off

pushd util\genGlFunctionLoader
call build-windows.bat
popd

set projectName=shader-baker

set buildDir=build
set outputDir=%buildDir%\win32
set srcDir=..\..\src

set debugOptions=/MTd /Ob0 /Od /Zi
set releaseOptions=/MT /O2 /Oi

set ignoredWarnings=/wd4100

set libraries=Gdi32.lib opengl32.lib User32.lib

mkdir %outputDir% 2> nul

pushd %outputDir% > nul
cl.exe /nologo /W4 /WX %ignoredWarnings% %debugOptions% /Gm- %srcDir%\win32.cpp /Fd%projectName% /Fe%projectName% /INCREMENTAL:NO %libraries%
popd

