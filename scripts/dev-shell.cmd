@echo off
rem Development build shell for Transfer Fabric on this machine.
rem Loads the MSVC toolchain (vcvars64) and the CUDA runtime on PATH, then
rem executes the given command. Use for every CMake / build / test / run step.
if "%TF_VCVARS%"=="" set "TF_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if "%TF_CUDA_BIN%"=="" set "TF_CUDA_BIN=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin"
call "%TF_VCVARS%" >nul 2>&1
set "PATH=%TF_CUDA_BIN%;%PATH%"
%*
