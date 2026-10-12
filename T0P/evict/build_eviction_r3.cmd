@echo off
setlocal
set "HERE=%~dp0"
set "ROOT=%HERE%..\.."
set "VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 -vcvars_ver=14.44.35207 >nul
if errorlevel 1 exit /b 1
cd /d "%HERE%"
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /arch:AVX2 /D_WIN32_WINNT=0x0A00 evict.cpp /Fo:evict_r3.obj > build_eviction_r3.log 2>&1
if errorlevel 1 (type build_eviction_r3.log & exit /b 1)
cl /nologo /c /O2 /std:c++17 /EHsc /MD /DNDEBUG /arch:AVX2 /D_WIN32_WINNT=0x0A00 evict_qualification_r3.cpp /Fo:evict_qualification_r3.obj >> build_eviction_r3.log 2>&1
if errorlevel 1 (type build_eviction_r3.log & exit /b 1)
link /INCREMENTAL:NO evict_r3.obj evict_qualification_r3.obj advapi32.lib /OUT:evict_qualification_r3.exe >> build_eviction_r3.log 2>&1
if errorlevel 1 (type build_eviction_r3.log & exit /b 1)
dumpbin /disasm evict_r3.obj > evict_r3_disasm.txt
if errorlevel 1 exit /b 1
echo EVICT_BUILD=PASS LINKED_OBJECT=evict_r3.obj
certutil -hashfile evict.cpp SHA256 | findstr /r /v "hash CertUtil" > evict_source_sha256.txt
certutil -hashfile evict_r3.obj SHA256 | findstr /r /v "hash CertUtil" > evict_object_sha256.txt
certutil -hashfile evict_qualification_r3.exe SHA256 | findstr /r /v "hash CertUtil" > evict_executable_sha256.txt
exit /b 0
