# DarkAges MMO - C++ Compiler Setup Guide

## Recommended Compilers (In Order of Preference)

### Option 1: Visual Studio 2022 Build Tools (MSVC) - RECOMMENDED ⭐

**Why MSVC?**
- Best C++20 support on Windows
- Native Visual Studio debugger integration
- Industry standard for Windows game development
- Optimized for Windows performance
- Excellent IDE support (Visual Studio)

**Installation Steps:**

1. **Download the Build Tools:**
   ```powershell
   # Option A: Via winget (if available)
   winget install Microsoft.VisualStudio.2022.BuildTools
   
   # Option B: Direct download
   Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vs_buildtools.exe" -OutFile "vs_buildtools.exe"
   ```

2. **Install C++ workload:**
   ```powershell
   # Run the installer with C++ tools
   vs_buildtools.exe --quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.22621
   ```

3. **Verify Installation:**
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
   cl.exe
   ```

**Expected Output:**
```
Microsoft (R) C/C++ Optimizing Compiler Version 19.38.33133 for x64
Copyright (C) Microsoft Corporation.  All rights reserved.
```

---

### Option 2: LLVM/Clang 17+ - MODERN ALTERNATIVE

**Why Clang?**
- Excellent C++20 support
- Better error messages than MSVC
- Cross-platform compatible
- Great static analysis tools
- Can use with Visual Studio Code

**Installation Steps:**

1. **Download LLVM:**
   ```powershell
   # Via winget
   winget install LLVM.LLVM
   
   # Or direct download from:
   # https://github.com/llvm/llvm-project/releases
   ```

2. **Verify Installation:**
   ```powershell
   clang++ --version
   ```

**Expected Output:**
```
clang version 17.0.6
Target: x86_64-pc-windows-msvc
Thread model: posix
```

---

### Option 3: MinGW-w64 (GCC 13+) - OPEN SOURCE

**Why MinGW?**
- Open source (GPL)
- GCC compatibility
- Smaller install size
- Good for cross-platform development

**Installation Steps:**

1. **Download MSYS2 (recommended):**
   ```powershell
   # Download from https://www.msys2.org/
   # Or use winget
   winget install MSYS2.MSYS2
   ```

2. **Install GCC via MSYS2:**
   ```bash
   # In MSYS2 terminal
   pacman -S mingw-w64-ucrt-x86_64-gcc
   pacman -S mingw-w64-ucrt-x86_64-cmake
   pacman -S mingw-w64-ucrt-x86_64-ninja
   ```

3. **Add to PATH:**
   ```powershell
   [Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\msys64\ucrt64\bin", "User")
   ```

4. **Verify Installation:**
   ```powershell
   g++ --version
   ```

**Expected Output:**
```
g++ (Rev3, Built by MSYS2 project) 13.2.0
Copyright (C) 2023 Free Software Foundation, Inc.
```

---

## Project Configuration for Each Compiler

### MSVC Configuration (Default)

```cmake
# In CMakeLists.txt - already configured
cmake .. -G "Visual Studio 17 2022" -A x64
# or
cmake .. -G "NMake Makefiles"
# or
cmake .. -G "Ninja Multi-Config"
```

### Clang Configuration

```cmake
# Set Clang as compiler before running CMake
$env:CXX = "clang++"
$env:CC = "clang"
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
```

### MinGW/GCC Configuration

```cmake
# Set GCC as compiler before running CMake
$env:CXX = "g++"
$env:CC = "gcc"
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
```

---

## Automated Setup Script

Create `setup_compiler.ps1`:

```powershell
#!/usr/bin/env powershell
# DarkAges Compiler Setup Script

param(
    [ValidateSet("MSVC", "Clang", "MinGW")]
    [string]$Compiler = "MSVC"
)

Write-Host "Setting up $Compiler compiler for DarkAges MMO..." -ForegroundColor Green

switch ($Compiler) {
    "MSVC" {
        # Try to find MSVC
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            $installPath = & $vswhere -latest -property installationPath
            if ($installPath) {
                Import-Module "$installPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
                Enter-VsDevShell -VsInstallPath $installPath -SkipAutomaticLocation
                Write-Host "MSVC environment loaded!" -ForegroundColor Green
                cl.exe
            } else {
                Write-Error "MSVC not found. Please install Visual Studio Build Tools."
            }
        } else {
            Write-Error "Visual Studio not found. Install from: https://visualstudio.microsoft.com/downloads/"
        }
    }
    
    "Clang" {
        $clang = Get-Command clang++ -ErrorAction SilentlyContinue
        if ($clang) {
            $env:CXX = "clang++"
            $env:CC = "clang"
            Write-Host "Clang configured!" -ForegroundColor Green
            clang++ --version
        } else {
            Write-Error "Clang not found. Install with: winget install LLVM.LLVM"
        }
    }
    
    "MinGW" {
        $gcc = Get-Command g++ -ErrorAction SilentlyContinue
        if ($gcc) {
            $env:CXX = "g++"
            $env:CC = "gcc"
            Write-Host "MinGW/GCC configured!" -ForegroundColor Green
            g++ --version
        } else {
            Write-Error "MinGW not found. Install MSYS2 from: https://www.msys2.org/"
        }
    }
}

Write-Host "`nYou can now build with: .\build.ps1" -ForegroundColor Cyan
```

---

## Build Commands Summary

### Using MSVC (Recommended)
```powershell
# Load MSVC environment first
.\setup_compiler.ps1 -Compiler MSVC

# Build
.\build.ps1 -Test
```

### Using Clang
```powershell
# Set compiler
.\setup_compiler.ps1 -Compiler Clang

# Build
.\build.ps1 -Test
```

### Using MinGW
```powershell
# Set compiler
.\setup_compiler.ps1 -Compiler MinGW

# Build
.\build.ps1 -Test
```

---

## Troubleshooting

### Issue: "No CMAKE_CXX_COMPILER found"
**Solution:** Run `setup_compiler.ps1` first to set up the compiler environment

### Issue: "C++20 features not supported"
**Solution:** Update your compiler:
- MSVC: 2022 17.4 or later
- Clang: 15.0 or later
- GCC: 12.0 or later

### Issue: "Cannot find vcvarsall.bat"
**Solution:** Install Visual Studio Build Tools with C++ workload:
```powershell
vs_buildtools.exe --add Microsoft.VisualStudio.Workload.VCTools
```

---

## Recommended IDE Setup

### Visual Studio 2022 (Best with MSVC)
1. Install Visual Studio 2022 Community (free)
2. Select "Desktop development with C++" workload
3. Open `CMakeLists.txt` as project
4. Build with Ctrl+Shift+B

### Visual Studio Code (Works with all compilers)
1. Install C/C++ extension
2. Install CMake Tools extension
3. Select your compiler kit (Ctrl+Shift+P → "CMake: Select Kit")
4. Build with F7

### CLion (JetBrains)
1. Open project root
2. CMake will auto-detect compilers
3. Select toolchain in Settings → Build → Toolchains

---

## CI/CD Compiler Matrix

The `.github/workflows/build-and-test.yml` already tests with:
- MSVC (Windows)
- GCC (Ubuntu)
- Clang (Ubuntu)

This ensures code compiles across all major compilers.

---

## Next Steps

1. **Choose your compiler** (we recommend MSVC for Windows)
2. **Install using instructions above**
3. **Run `setup_compiler.ps1`**
4. **Build with `build.ps1 -Test`**
5. **Verify all tests pass**

---

*For help, see: https://github.com/DarkAges/DarkAges/discussions*
