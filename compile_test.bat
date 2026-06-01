@echo off
cd /d D:\d\git\disk-recover
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe" /nologo /c /std:c++20 /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Isrc /Isrc\ui\gui /Ibuild\vcpkg_installed\x64-windows\include /Fosave_dirs_dialog_test.obj src\ui\gui\save_dirs_dialog.cpp
