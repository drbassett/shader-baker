@echo off

set buildDir=build
set outputDir=%buildDir%\win32
set srcDir=..\..\src
set fontRasterizerDir=util\fontRasterizer

setlocal

pushd util\genGlFunctionLoader
call build-windows.bat || goto:popdErrorExit
popd

pushd %fontRasterizerDir% > nul
call build-windows.bat || goto:popdErrorExit
popd

endlocal

mkdir %outputDir% 2> nul

copy shaders\* %outputDir% > nul

set ttfFileName=C:\Windows\Fonts\Arial.ttf
%fontRasterizerDir%\build\rasterize-font %ttfFileName% %outputDir%\arial.font || goto:errorExit

set projectName=shader-baker

set debugOptions=/MTd /Ob0 /Od /Zi
set releaseOptions=/MT /O2 /Oi

set ignoredWarnings=/wd4100 /wd4996

set libraries=Gdi32.lib opengl32.lib User32.lib

pushd %outputDir% > nul
cl.exe /nologo /W4 /WX %ignoredWarnings% %debugOptions% /Gm- %srcDir%\win32.cpp /Fd%projectName% /Fe%projectName% /link /INCREMENTAL:NO %libraries%
popd

goto:eof

:popdErrorExit
	popd
:errorExit
	EXIT /B 1

