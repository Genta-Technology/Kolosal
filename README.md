Below is a polished description of **Kolosal AI** that you can include in your README or project page. Feel free to adjust the wording to best suit your style or project needs.

---

## Kolosal AI

**Kolosal AI** is an open-source desktop application designed to simplify the training and inference of large language models on your own device. It supports any CPU with **AVX2** instructions and also works with **AMD** and **NVIDIA** GPUs. Built to be lightweight (only ~20 MB compiled), **Kolosal AI** runs smoothly on most edge devices, enabling on-premise or on-edge AI solutions without heavy cloud dependencies.

- **License:** [Apache 2.0](https://www.apache.org/licenses/LICENSE-2.0)
- **Developer:** [Genta Technology](https://genta.tech)
- **Community:** [Join our Discord](https://discord.gg/XDmcWqHmJP)

### Key Features

1. **Universal Hardware Support**  
   - AVX2-enabled CPUs  
   - AMD and NVIDIA GPUs

2. **Lightweight & Portable**  
   - Compiled size ~20 MB  
   - Ideal for edge devices like Raspberry Pi or low-power machines

3. **Wide Model Compatibility**  
   - Supports popular models like **Mistral**, **LLaMA**, **Qwen**, and many more  
   - Powered by the [Genta Personal Engine](https://github.com/ggerganov/llama.cpp) built on top of [Llama.cpp](https://github.com/ggerganov/llama.cpp)

4. **Easy Dataset Generation & Training**  
   - Build custom datasets with minimal overhead  
   - Train models using **UnsLOTH** or other frameworks  
   - Deploy locally or as a server in just a few steps

5. **On-Premise & On-Edge Focus**  
   - Keeps data private on your own infrastructure  
   - Lowers costs by avoiding expensive cloud-based solutions

### Use Cases

- **Local AI Inference:** Quickly run LLMs on your personal laptop or desktop for offline or on-premise scenarios.  
- **Edge Deployment:** Bring large language models to devices with limited resources, ensuring minimal latency and improved privacy.  
- **Custom Model Training:** Simplify the process of data preparation and model training without relying on cloud hardware.

---

## About Genta Technology

We are a small team of students passionate about addressing key concerns in AI such as **energy**, **privacy**, **on-premise**, and **on-edge** computing. Our flagship product is the **Genta Inference Engine**, which allows enterprises to deploy open-source models on their own servers, with **3-4x higher throughput**. This can reduce operational costs by up to **80%**, as a single server optimized by our engine can handle the workload of four standard servers.

---

### Get Involved

1. **Clone the Repository**: [https://github.com/Genta-Technology/Kolosal]
2. **Join the Community**: Ask questions, propose features, and discuss development on our [Discord](https://discord.gg/XDmcWqHmJP).  
3. **Contribute**: We welcome pull requests, bug reports, feature requests, and any kind of feedback to improve **Kolosal AI**.

---

## How to Compile

1. [Project Overview](#project-overview)
2. [Directory Structure](#directory-structure)
3. [Prerequisites](#prerequisites)
4. [Cloning and Preparing the Repository](#cloning-and-preparing-the-repository)
5. [Configuring the Project with CMake](#configuring-the-project-with-cmake)
6. [Building the Application](#building-the-application)
7. [Running the Application](#running-the-application)
8. [Troubleshooting](#troubleshooting)

---

## Project Overview

- **Name:** Kolosal AI (Desktop application target is `KolosalDesktop`)
- **Language Standard:** C++17
- **Build System:** [CMake](https://cmake.org/) (version 3.14 or higher)
- **Dependencies** (automatically handled by the provided `CMakeLists.txt`, if placed in correct directories):
  - OpenGL
  - OpenSSL
  - CURL
  - GLAD
  - Native File Dialog Extended
  - genta-personal engine libraries (InferenceEngineLib, InferenceEngineLibVulkan)
  - ImGui (provided in `external/imgui`)
  - Other external libraries: `stb`, `nlohmann/json`, `icons`, etc.

## Directory Structure

A simplified look at the important folders/files:

```
KolosalAI/
├─ cmake/
│   └─ ucm.cmake                 # Utility script for static runtime linking
├─ external/
│   ├─ curl/                     # Pre-built or source for cURL
│   ├─ glad/                     # GLAD loader
│   ├─ genta-personal/           # genta-personal engine includes/libs
│   ├─ imgui/                    # ImGui source
│   ├─ nativefiledialog-extended # Native File Dialog Extended
│   ├─ nlohmann/                 # JSON library
│   ├─ stb/                      # stb (single-file) headers
│   └─ fonts/                    # TrueType fonts
├─ assets/
│   ├─ logo.png
│   └─ resource.rc               # Windows resource file
├─ source/
│   └─ main.cpp                  # Entry point for KolosalDesktop
├─ include/
│   └─ ... (additional headers)
├─ models/
│   └─ ... (model.json configuration files used by the inference engine to download, save, and load the model engine)
├─ CMakeLists.txt
├─ README.md                     # You are here!
└─ ...
```

## Prerequisites

1. **CMake 3.14 or above**  
   Download from [https://cmake.org/download/](https://cmake.org/download/).
2. **A C++17-compatible compiler**  
   - For Windows, Visual Studio 2019/2022 (MSVC) or [MinGW-w64](https://www.mingw-w64.org/) with GCC 7+.
   - For other platforms, an equivalent compiler supporting C++17.
3. **Git** (optional, but recommended for cloning and submodule management).
4. **OpenSSL**, **CURL**  
   - On Windows, you can place the pre-built bins/headers inside `external/openssl` and `external/curl` (or anywhere you prefer, just ensure `CMakeLists.txt` sees them).
5. **(Optional) Vulkan SDK** if you plan to use the Vulkan-based inference engine.

## Cloning and Preparing the Repository

1. **Clone the repository**:

   ```bash
   git clone https://github.com/<your_username_or_org>/KolosalAI.git
   cd KolosalAI
   ```

2. **(Optional) Update submodules**:  
   If any external libraries are handled as Git submodules, initialize them:

   ```bash
   git submodule update --init --recursive
   ```

3. **Check external dependencies**:  
   Ensure the `external` folder contains:
   - `curl` with `include/`, `lib/`, and `bin/` (Windows).
   - `openssl` or that OpenSSL is installed system-wide.
   - The `genta-personal` engine in place if not fetched from elsewhere.

4. **Folder structure verification**:  
   Verify that folders like `nativefiledialog-extended`, `imgui`, etc., are present inside `external/`.

## Configuring the Project with CMake

You can perform either an in-source or out-of-source build, but **out-of-source** is recommended. Below is an example of an out-of-source build:

1. **Create a build folder**:

   ```bash
   mkdir build
   cd build
   ```

2. **Run CMake**:  
   By default, this will generate build files for your platform’s default generator (e.g., Visual Studio solution files on Windows, Makefiles on Linux, etc.):

   ```bash
   cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
   ```

   or explicitly (for Visual Studio multi-config):

   ```bash
   cmake -S .. -B . -G "Visual Studio 17 2022" -A x64
   ```

   - `-DDEBUG=ON` can be used if you want to build a debug version:

     ```bash
     cmake -S .. -B . -DCMAKE_BUILD_TYPE=Debug -DDEBUG=ON
     ```

3. **Check for any errors** during configuration, such as missing libraries or headers. Resolve them by installing or copying the required dependencies into the correct location.

## Building the Application

After successful configuration:

- **On Windows with Visual Studio**:  
  Open the generated `.sln` file inside `build/` and build the solution. Or build from the command line using:

  ```bash
  cmake --build . --config Release
  ```

- **On other platforms** (e.g., using Make or Ninja):

  ```bash
  cmake --build . --config Release
  ```

> **Note**:  
> The `POST_BUILD` commands in `CMakeLists.txt` will copy the necessary DLLs, fonts, assets, and models into the final output folder (e.g., `build/Release/` or `build/Debug/`, depending on your generator).

## Running the Application

1. **Locate the output**:  
   Once the build completes, you should find the executable (e.g., `KolosalDesktop.exe` on Windows) in a directory such as:
   - `build/Release/` (Visual Studio).
   - `build/` (single-config generators like Make).

2. **Check for required files**:  
   The post-build commands should have copied:
   - **Fonts** (`/fonts` folder next to the exe).
   - **Assets** (`/assets` folder next to the exe).
   - **Models** (`/models` folder next to the exe).
   - **OpenSSL** and **InferenceEngine** DLLs (Windows).
   - **cURL** DLL(s) (Windows).

   Make sure these folders and files are present in the same directory as `KolosalDesktop.exe`.

3. **Double-click or run from terminal**:

   ```bash
   cd build/Release
   ./KolosalDesktop.exe
   ```

4. **Enjoy Kolosal AI**!

## Troubleshooting

1. **OpenSSL or CURL not found**  
   - Make sure you have them installed or placed in `external/openssl` and `external/curl` respectively.  
   - Check environment variables like `OPENSSL_ROOT_DIR` or `CURL_ROOT_DIR` if needed.  
   - Update `CMAKE_PREFIX_PATH` if you’re placing these libraries somewhere non-standard.

2. **InferenceEngine libraries not found**  
   - Verify the path `external/genta-personal/lib` actually contains `InferenceEngineLib.lib` or `InferenceEngineLibVulkan.lib` (on Windows).  
   - Adjust `find_library` paths in `CMakeLists.txt` if your structure differs.

3. **Missing Vulkan SDK**  
   - If you plan to use the Vulkan-based inference engine, ensure Vulkan SDK is installed and available in your PATH or that CMake can find it.

4. **ImGui not found**  
   - Ensure `external/imgui` folder is not empty.  
   - If you see compilation errors referencing ImGui headers, check that `target_include_directories` in `CMakeLists.txt` still points to the correct path.

5. **Resource or Icon issues on non-Windows**  
   - The `assets/resource.rc` file is Windows-specific. For Linux/macOS builds, you can comment out or remove references to `.rc` if they cause issues.

6. **Runtime errors** due to missing DLLs or dynamic libraries  
   - Confirm that the post-build step copies all required `.dll` files next to the executable.  
   - On Linux/macOS, ensure `.so`/`.dylib` are in the library search path or same folder.
