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

echo [2/4] Compiling src\main.c + audio.c + disk.c + mli.s (Slot 2 and Slot 4) ...
call "%CC%" -Oz -T src\link1000.ld -DUSE_PSG_THEME -o build\main.bin src\main.c src\audio.c src\disk.c src\mli.s
if errorlevel 1 goto fail
call "%CC%" -Oz -T src\link1000.ld -DUSE_PSG_THEME -DVERA_BASE=0xC400 -o build\main4.bin src\main.c src\audio.c src\disk.c src\mli.s
if errorlevel 1 goto fail

echo [3/4] Building ProDOS HDV (TimePilot-IIvera.hdv) ...
call node tools\build_hdv.mjs
if errorlevel 1 goto fail

echo [4/4] Building Dual 140KB Floppy Disks (TimePilot-IIvera-D1.po + TimePilot-IIvera-D2.po) ...
call node tools\build_disk.mjs
if errorlevel 1 goto fail

echo.
echo OK:
echo   - TimePilot-IIvera.hdv   (800KB ProDOS Hard Disk - 4-Voice PSG theme)
echo   - TimePilot-IIvera-D1.po (140KB 5.25" Floppy Disk 1: Boot + Slot 2 Binary + Art)
echo   - TimePilot-IIvera-D2.po (140KB 5.25" Floppy Disk 2: PCM Audio + Slot 4 Binary)
echo.
echo Dual-drive loading: Mount D1 in Drive 1 and D2 in Drive 2; boot Drive 1.
endlocal & exit /b 0

:fail
endlocal & exit /b 1
