@echo off
rem Shared deploy logic: checks the PHP files, packs the site and uploads it over FTPS or SFTP.
rem Called by deploy_test.cmd / deploy_production.cmd:  call docker\deploy.cmd <target> [test]
rem   <target> = test | production  -> settings in deploy_<target>.env (example: deploy_<target>.env.example)
rem   test     = only check the connection
setlocal
set TARGET=%~1
set ENVFILE=deploy_%TARGET%.env
rem Docker tries to add git info to the image - without git.exe in PATH it only prints a warning
set BUILDX_GIT_INFO=0

docker info >nul 2>&1
if errorlevel 1 (
  echo Docker is not running - start Docker Desktop and try again.
  exit /b 1
)

if not exist %ENVFILE% (
  copy %ENVFILE%.example %ENVFILE% >nul
  echo Created %ENVFILE% - enter the password ^(and the user name if it is missing^), save it and run the script again.
  start "" notepad %ENVFILE%
  exit /b 1
)

echo Preparing the upload tool (lftp)...
docker build -q --target deploy -t hinasto-deploy . >nul
if errorlevel 1 (
  echo Could not prepare the container - see the message above.
  exit /b 1
)

if /i "%~2"=="test" goto upload

echo.
echo Checking the PHP syntax and building the package...
if exist dist rmdir /s /q dist
docker build --target dist --output dist .
if errorlevel 1 (
  echo.
  echo Building the package FAILED - nothing was uploaded to the server.
  exit /b 1
)

:upload
echo.
docker run --rm --env-file %ENVFILE% -v "%cd%\dist:/dist:ro" hinasto-deploy %2
if errorlevel 1 (
  echo.
  echo Upload FAILED - see the message above.
  exit /b 1
)
exit /b 0
