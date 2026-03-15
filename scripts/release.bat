@echo off
setlocal enabledelayedexpansion

:: ========================================
:: Mini Scale Release Script
:: Создание релиза с тегом и ZIP-архивом
:: ========================================

:: Параметры
set VERSION=%1
if "%VERSION%"=="" (
    echo.
    echo Usage: release.bat ^<version^>
    echo Example: release.bat v1.6.1
    echo.
    exit /b 1
)

echo ========================================
echo Mini Scale Release: %VERSION%
echo ========================================
echo.

:: 1. Проверка чистоты working tree
echo [1/6] Checking working tree...
git diff --quiet || (
    echo.
    echo ERROR: Working tree not clean!
    echo Commit or stash changes first:
    echo   git add .
    echo   git commit -m "message"
    echo.
    exit /b 1
)
echo OK: Working tree clean.

:: 2. Проверка наличия CHANGELOG.md
echo [2/6] Checking CHANGELOG.md...
if not exist CHANGELOG.md (
    echo ERROR: CHANGELOG.md not found!
    exit /b 1
)
echo OK: CHANGELOG.md exists.

:: 3. Проверка Config.h
echo [3/6] Checking Config.h...
if not exist Mini_Scale\Config.h (
    echo ERROR: Config.h not found!
    exit /b 1
)
echo OK: Config.h exists.

:: 4. Напоминание об обновлении версий
echo.
echo [4/6] Version check reminder:
echo   - Config.h: FIRMWARE_VERSION
echo   - Config.h: FW_VERSION_STR = "%VERSION%"
echo   - CHANGELOG.md updated
echo.
set /p CONFIRM="Continue? (y/n): "
if /i not "!CONFIRM!"=="y" (
    echo Aborted.
    exit /b 1
)

:: 5. Создание коммита и тега
echo [5/6] Creating commit and tag...
git add CHANGELOG.md Mini_Scale\Config.h
git commit -m "Release %VERSION%"
if errorlevel 1 (
    echo No changes to commit (tag only).
)

git tag -a %VERSION% -m "Release %VERSION%"
if errorlevel 1 (
    echo.
    echo ERROR: Tag %VERSION% already exists!
    exit /b 1
)
echo OK: Tag created.

:: 6. Создание ZIP-архива
echo [6/6] Creating ZIP archive...

:: Создать временную папку
set ARCHIVE_NAME=Mini_Scale_%VERSION%
mkdir "..\releases\%ARCHIVE_NAME%" 2>nul

:: Копировать файлы
xcopy /E /I /Y /Q "Mini_Scale" "..\releases\%ARCHIVE_NAME%\Mini_Scale"
xcopy /E /I /Y /Q "*.md" "..\releases\%ARCHIVE_NAME%\"

:: Архивировать (используем встроенный PowerShell)
cd "..\releases"
powershell -Command "Compress-Archive -Path '%ARCHIVE_NAME%' -DestinationPath '%ARCHIVE_NAME%.zip' -Force"
rmdir /S /Q "%ARCHIVE_NAME%"
cd "..\Mini_Scale"

echo.
echo ========================================
echo Release %VERSION% created successfully!
echo ========================================
echo.
echo Archive: ..\releases\%ARCHIVE_NAME%.zip
echo.
echo Next steps:
echo   git push origin %VERSION%
echo   git push origin main
echo.

endlocal
