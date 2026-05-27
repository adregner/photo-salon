# photo-salon

A cross-platform minimalistic desktop photo viewing application to facilitate in-person photography salons.

## Prerequisites

### Linux (Debian/Ubuntu)

CMake and a C++ compiler are required. Qt 6.11+ will be fetched automatically by the build script if the system version is too old.

```bash
sudo apt install cmake g++
```

### macOS

- [Homebrew](https://brew.sh/)
- Qt 6.11+ and CMake:

```bash
brew install qt cmake
```

### Windows
- [Qt6](https://www.qt.io/download) via Qt Online Installer — select the MSVC 2022 64-bit component
- [CMake](https://cmake.org/download/)
- Visual Studio 2022 (Community edition is sufficient)

## Building

### Linux / macOS

```bash
./build
```

On Linux, if Qt 6.11+ is not found the script automatically downloads it via
[`fetch-linux-qt.sh`](fetch-linux-qt.sh) and builds against it.

On macOS, if Qt 6.11+ is not found the script prints the Homebrew command to
install it and exits.

### Windows (native)

Open a **Developer Command Prompt for VS 2022**, then:

```powershell
cmake -B _build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64
cmake --build _build --config Release
```

Replace `6.x.x` with the Qt version you installed (e.g. `6.8.0`).

### Windows (cross-compile from macOS)

Cross-compiles a self-contained static `.exe` using `clang-cl` targeting the MSVC ABI. See [`WINDOWS.md`](WINDOWS.md) for prerequisites and setup.

```bash
./build-windows.sh
# → _build_win/photo-salon.exe
```

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
