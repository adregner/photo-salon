# photo-salon

A cross-platform desktop image viewer built with C++ and Qt6.

## Prerequisites

### Linux (Debian/Ubuntu)

```bash
sudo apt install qt6-base-dev qt6-base-dev-tools cmake g++
```

### macOS
- [Homebrew](https://brew.sh/)
- Qt6 and CMake: `brew install qt cmake`

### Windows
- [Qt6](https://www.qt.io/download) via Qt Online Installer — select the MSVC 2022 64-bit component
- [CMake](https://cmake.org/download/)
- Visual Studio 2022 (Community edition is sufficient)

## Building

### Linux / macOS

```bash
cmake -B build
cmake --build build
```

On macOS, if Qt6 is not found automatically:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
```

### Windows

Open a **Developer Command Prompt for VS 2022**, then:

```powershell
cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64
cmake --build build --config Release
```

Replace `6.x.x` with the Qt version you installed (e.g. `6.8.0`).

## Running

### Linux / macOS

```bash
./build/photo-salon /path/to/image.jpg
```

### Windows

```powershell
.\build\Release\photo-salon.exe C:\path\to\image.jpg
```

## Usage

```
photo-salon <image.jpg>
```

Opens the specified JPEG image in a resizable window, scaled to fit.
