@echo off
setlocal enabledelayedexpansion

:: ========================================
:: Mini Scale Backup Script
:: Создание backup-ветки перед изменениями
:: ========================================

:: Получить текущую дату
for /f "tokens=2 delims==" %%I in ('wmic os get localdatetime /value') do set datetime=%%I
set DATE=%datetime:~0,4%-%datetime:~4,2%-%datetime:~6,2%

set BRANCH_NAME=backup-%DATE%

echo ========================================
echo Mini Scale Backup
echo ========================================
echo.

:: Проверка текущей ветки
echo [1/3] Checking current branch...
for /f "tokens=*" %%i in ('git rev-parse --abbrev-ref HEAD') do set CURRENT_BRANCH=%%i
echo Current branch: %CURRENT_BRANCH%

:: Сохранить текущие изменения (если есть)
echo [2/3] Checking for uncommitted changes...
git diff --quiet && git diff --cached --quiet
if errorlevel 1 (
    echo.
    echo WARNING: Uncommitted changes detected.
    set /p STASH="Stash and commit later? (y/n): "
    if /i "!STASH!"=="y" (
        git stash push -m "backup-stash-%DATE%"
        echo Stash created.
    ) else (
        echo Proceeding without stash.
    )
) else (
    echo OK: No uncommitted changes.
)

:: Создать backup-ветку
echo [3/3] Creating backup branch: %BRANCH_NAME%...

git branch | findstr /C:"%BRANCH_NAME%"
if not errorlevel 1 (
    echo.
    echo WARNING: Branch %BRANCH_NAME% already exists!
    set /p OVERWRITE="Force recreate? (y/n): "
    if /i "!OVERWRITE!"=="y" (
        git branch -D %BRANCH_NAME%
    ) else (
        echo Aborted.
        exit /b 1
    )
)

git checkout -b %BRANCH_NAME%
git checkout %CURRENT_BRANCH%

echo.
echo ========================================
echo Backup created successfully!
echo ========================================
echo.
echo Backup branch: %BRANCH_NAME%
echo Current branch: %CURRENT_BRANCH%
echo.
echo To restore backup:
echo   git checkout %BRANCH_NAME%
echo.

endlocal
