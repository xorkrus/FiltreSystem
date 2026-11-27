@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
cd /d "%~dp0"

set FQBN=esp32:esp32:esp32c3
set ARDUINO_DIRECTORIES_DATA=D:\Soft\arduino-cli\ArduinoData
set BOARD_OPTS=CDCOnBoot=cdc,CPUFreq=80
set MKLITTLEFS="D:\Soft\arduino-cli\ArduinoData\packages\esp32\tools\mklittlefs\4.0.2-db0513a\mklittlefs.exe"
set BUILD=build
set IP=192.168.1.89
set FW=%BUILD%\FiltreSystem.ino.bin
set FS=%BUILD%\littlefs.bin

echo.
echo === [1/2] Компиляция прошивки ===
arduino-cli compile --fqbn %FQBN% --board-options "%BOARD_OPTS%" --output-dir %BUILD% . --no-color
if errorlevel 1 ( echo Прошивка: ОШИБКА сборки & pause & exit /b 1 )

echo.
echo === [2/2] Сборка LittleFS ===
if not exist "%BUILD%" mkdir "%BUILD%"
%MKLITTLEFS% -c data -p 256 -b 4096 -s 0x30000 "%FS%"
if errorlevel 1 ( echo LittleFS: ОШИБКА сборки & pause & exit /b 1 )

echo.
echo === Готово ===
echo Прошивка:  %FW%
echo LittleFS:  %FS%
echo.

choice /C YN /M "Автозаливка через curl сейчас"
if errorlevel 2 goto :end

call :ota "%FW%" fr firmware
timeout /t 15 /nobreak >nul
call :ota "%FS%" fs filesystem

echo.
echo Готово. Открывай http://%IP%/
goto :end

:ota
set FILE=%~1
set MODE=%~2
set LABEL=%~3
set HASH=

for /f "tokens=* delims=" %%a in ('certutil -hashfile "%FILE%" MD5 ^| findstr /r /c:"^[0-9a-f][0-9a-f]*$"') do (
  set HASH=%%a
)
set HASH=%HASH: =%

echo.
echo [OTA] %LABEL%: start (md5=%HASH%)
curl -s -o nul -w "  start HTTP %%{http_code}\n" "http://%IP%/ota/start?mode=%MODE%&hash=%HASH%"

echo [OTA] %LABEL%: upload
curl -s -o nul -w "  upload HTTP %%{http_code}\n" -F "file=@%FILE%;type=application/octet-stream" "http://%IP%/ota/upload"

set HASH=
exit /b

:end
pause