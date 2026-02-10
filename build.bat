@echo off
REM Windows Shell - Build and Test Script
REM ============================================

echo ============================================
echo Windows Mini Shell - Build Script
echo ============================================
echo.

REM Check for g++ (MinGW)
where g++ >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Found: MinGW g++
    echo Compiling with g++...
    g++ -std=c++17 -o shell.exe minishell_windows_full.cpp
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ============================================
        echo Compilation Successful!
        echo ============================================
        echo.
        echo Executable: shell.exe
        echo.
        echo To run: shell.exe
        echo.
        goto :test
    ) else (
        echo.
        echo ERROR: Compilation failed!
        goto :end
    )
)

REM Check for cl (MSVC)
where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo Found: Microsoft Visual C++
    echo Compiling with cl...
    cl /EHsc /std:c++17 /Fe:shell.exe minishell_windows_full.cpp
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo ============================================
        echo Compilation Successful!
        echo ============================================
        echo.
        goto :test
    ) else (
        echo.
        echo ERROR: Compilation failed!
        goto :end
    )
)

REM No compiler found
echo.
echo ERROR: No C++ compiler found!
echo.
echo Please install one of:
echo   1. MinGW-w64: https://www.mingw-w64.org/
echo   2. MSYS2: https://www.msys2.org/
echo   3. Visual Studio Community Edition
echo.
goto :end

:test
REM Ask if user wants to run tests
echo Would you like to run basic tests? (Y/N)
set /p choice=

if /i "%choice%"=="Y" (
    echo.
    echo ============================================
    echo Running Basic Tests
    echo ============================================
    echo.
    
    REM Create test file
    echo apple > test_data.txt
    echo banana >> test_data.txt
    echo cherry >> test_data.txt
    echo apple >> test_data.txt
    
    echo Test 1: Basic command
    shell.exe < test_commands.txt
    
    echo.
    echo Test files created:
    echo   - test_data.txt
    echo.
    echo You can now run: shell.exe
)

:end
echo.
pause
