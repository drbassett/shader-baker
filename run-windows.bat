@echo off

pushd build\win32 > nul
shader-baker.exe ..\..\project.sb program
popd

