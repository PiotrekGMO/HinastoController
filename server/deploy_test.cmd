@echo off
rem Checks the PHP files and uploads the Hinasto Controller server to the TEST server over FTPS or SFTP.
rem Login and password: deploy_test.env (created from deploy_test.env.example on the first run).
rem   deploy_test.cmd        - check + upload
rem   deploy_test.cmd test   - only check the FTPS connection
cd /d "%~dp0"
call docker\deploy.cmd test %1
pause
