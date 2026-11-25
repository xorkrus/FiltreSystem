@echo off
arduino-cli compile --fqbn esp32:esp32:esp32c3 %1 --output-dir ./build --no-color