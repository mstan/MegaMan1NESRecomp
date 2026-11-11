@echo off
setlocal

git submodule update --init --recursive nesrecomp recomp-ui
if errorlevel 1 exit /b 1

echo Ready - nesrecomp and recomp-ui are checked out at their pinned commits.
