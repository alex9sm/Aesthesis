@echo off
if not exist "%~dp0spv" mkdir "%~dp0spv"
for %%f in ("%~dp0glsl\*.vert" "%~dp0glsl\*.frag" "%~dp0glsl\*.comp") do (
    echo Compiling %%~nxf...
    "C:\VulkanSDK\1.4.341.1\Bin\glslc.exe" "%%f" -o "%~dp0spv\%%~nxf.spv"
    if errorlevel 1 echo FAILED: %%~nxf
)
echo Done.
