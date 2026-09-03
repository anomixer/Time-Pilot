@echo off
rem Build Time Pilot IIvera: compile C+asm -> main.bin, pack audio, build HDV.
setlocal
set SDK=C:\dev\llvm-mos-sdk\install
set CC=%SDK%\bin\mos-apple2e-clang.bat
cd /d "%~dp0"

if not exist build mkdir build

echo [1/3] Packing PCM audio + sprite art ...
call node tools\mkpcm_blob.mjs
if errorlevel 1 goto fail
call node tools\mkart.mjs
if errorlevel 1 goto fail
call node tools\mkdemo_blob.mjs
if errorlevel 1 goto fail

echo [2/3] Compiling src\main.c + audio.c + disk.c + mli.s (Slot 2 + Slot 4) ...
call "%CC%" -Os -T src\link1000.ld -o build\main.bin src\main.c src\audio.c src\disk.c src\mli.s
if errorlevel 1 goto fail
call "%CC%" -Os -T src\link1000.ld -DVERA_BASE=0xC400 -o build\main4.bin src\main.c src\audio.c src\disk.c src\mli.s
if errorlevel 1 goto fail

echo [3/3] Building ProDOS HDV ...
call node tools\build_hdv.mjs
if errorlevel 1 goto fail

echo.
echo OK: TimePilot-IIvera.hdv
echo Load TimePilot-IIvera.hdv in Apple2TS; it boots to BASIC and BRUNs MAIN.BIN (or MAIN4.BIN).
endlocal & exit /b 0

:fail
endlocal & exit /b 1
