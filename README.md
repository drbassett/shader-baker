# shader-baker
GLSL shader editor with live preview

## History
The original [`shader-baker`](https://github.com/dboone/shader-baker.git) project was written in C# and WPF. The authors, @drbassett and @dboone, selected WPF because they thought it would be awesome. For certain things, WPF was indeed awesome. However, as the project grew, they found that too much of their time was spent figuring out some obsecure detail of WPF. In the interest of advancing the project, the authors decided that it would be best to use a more familiar language.

## Building
Currently, only a Windows build through MSVC is available. This project can be built for Windows by running [build-windows.bat](https://github.com/drbassett/shader-baker/blob/master/build-windows.bat) from the Windows command prompt. This requires first initializing the shell environment to satisfy the MSVC compiler. In order to do this, find your Visual Studio install directory, and run the batch file at `<vc-install>\VC\vcvarsall.bat x64`. The x64 is an argument to the command telling it to set up the 64-bit compiler.

