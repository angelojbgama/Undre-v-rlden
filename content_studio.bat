@echo off
setlocal
pushd "%~dp0"
python -m tools.content_studio %*
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%
