@echo off

pushd build\win32 > nul
set shadersDir=..\..\shaders
shader-baker.exe %shadersDir%\user-shader.vert %shadersDir%\user-shader.frag
popd

