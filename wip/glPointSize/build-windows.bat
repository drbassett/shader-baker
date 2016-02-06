@echo off

set projectName=glPointSize
set outputDir=build
set srcDir=..
set debugOptions=/LDd /Ob0 /Od /Zi
set releaseOptions=/LD /O2 /Oi
set libraries=Gdi32.lib opengl32.lib User32.lib

mkdir %outputDir% 2> nul

pushd %outputDir% > nul
del %projectName%.*
cl.exe /nologo /W4 /WX /Gm- %srcDir%\glPointSize.cpp %debugOptions% /Fd%projectName% /Fe%projectName% /INCREMENTAL:NO %libraries%
popd

pushd %outputDir% > nul
copy %projectName%.* ..\..\..\build\win32
popd