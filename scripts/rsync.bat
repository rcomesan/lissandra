@echo off
set USER=utnso
set HOST=127.0.0.1
SET PORT=6622

set SOLUTION_NAME=%1%
set SOLUTION_NAME=%SOLUTION_NAME:"=%
set SOLUTION_NAME=%SOLUTION_NAME:~0,-4%

set PROJECT_NAME=%2%
set PROJECT_NAME=%PROJECT_NAME:"=%
set PROJECT_NAME=%PROJECT_NAME:~0,-8%

set LOCAL_DIR=/cygdrive/c/%SOLUTION_NAME%/
set REMOTE_DIR=/home/utnso/%SOLUTION_NAME%/

set EXCLUSIONS=^
    --exclude=build^
    --exclude=bin^
    --exclude=obj^
    --exclude=.git^
    --exclude=.ssh^
    --exclude=.vs^
    --exclude=.settings^
    --exclude=*.sln^
    --exclude=*.vcxproj^
    --exclude=*.vcxproj.filters^
    --exclude=*.vcxproj.user

set COMMAND=cd %LOCAL_DIR%;rsync -av -e \"ssh -p%PORT%\" . %USER%@%HOST%:%REMOTE_DIR% --delete %EXCLUSIONS%
@echo on

"C:\cygwin64\bin\bash.exe" --login -c "%COMMAND%"

@echo off
echo ------------------------------------------------------------------------------------
echo Scynchronization OK!
