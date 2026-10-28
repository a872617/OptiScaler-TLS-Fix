@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
title OptiScaler TLS crash fix - patcher
echo ============================================================
echo   OptiScaler TLS crash fix
echo   by YuJiaMian (yu jia mian han hua)
echo.
echo   Patches the proxy DLL in place, keeps the original name.
echo   Supported names: dxgi winmm d3d12 dbghelp version wininet winhttp OptiScaler
echo ============================================================
echo.
set COUNT=0
for %%F in ("dxgi.dll" "winmm.dll" "d3d12.dll" "dbghelp.dll" "version.dll" "wininet.dll" "winhttp.dll" "OptiScaler.dll") do (
  if exist "%%~F" (
    set "FN=%%~nxF"
    for %%A in ("%%~F") do set "SZ=%%~zA"
    if !SZ! GEQ 20000000 (
      if exist "!FN!.bak" (
        echo [skip] !FN!  - !FN!.bak already exists
      ) else (
        echo.
        echo [patch] !FN!  ^(!SZ! bytes^)
        patcher.exe "!FN!" "__patched.tmp"
        if exist "__patched.tmp" (
          move /y "!FN!" "!FN!.bak" >nul
          move /y "__patched.tmp" "!FN!" >nul
          echo [ok  ] !FN!  - patched, original saved as !FN!.bak
          set /a COUNT+=1
        ) else (
          echo [fail] !FN!  - no supported site found, file untouched
        )
      )
    ) else (
      echo [skip] !FN!  - only !SZ! bytes, not an OptiScaler build
    )
  )
)
echo.
if !COUNT! EQU 0 (
  echo [INFO] Nothing patched.
  echo        Put the DLL produced by OptiScaler Client into this folder first.
) else (
  echo ------------------------------------------------------------
  echo   Done. !COUNT! file^(s^) patched, originals kept as .bak
  echo ------------------------------------------------------------
)
echo.
pause
