@echo off
rem Recettear free-play capture (OpenRecet rhost) - docs/plans/free-play-capture.md FPC-4
title Recettear capture
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0play.ps1" %*
pause
