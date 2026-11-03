# GSE GLFW integration

GLFW **3.4** is vendored from the official, pinned source archive:

https://codeload.github.com/glfw/glfw/zip/refs/tags/3.4

Archive SHA-256:

```text
A133DDC3D3C66143EBA9035621DB8E0BCF34DBA1EE9514A9E23E96AFD39FD57A
```

The complete upstream package (include, src, deps, CMake files, examples, tests,
documentation and license) is included unchanged. GSE adds `GLFW.vcxproj` and
this file. No source download, submodule, vcpkg installation, CMake installation
or prebuilt GLFW binary is required to build GSE with Visual Studio.

`GLFW.vcxproj` builds the Win32/WGL backend and common/null/EGL/OSMesa support
sources listed in upstream `src/CMakeLists.txt` as a **static library**.
It supports Debug/Release and Win32/x64, with matching /MDd or /MD runtime.
GLFW source is compiled as C11 without GSE's precompiled header.
`_GLFW_WIN32`, Unicode and the upstream MSVC CRT compatibility macro are private
to this library project. Do not define `GLFW_DLL` or GLFW header option macros
while building the library itself.

GSE references this project directly. MSBuild builds the correct architecture
first and links its output automatically. Generated files live under
`build/<Platform>/<Configuration>/` and are ignored by Git. Commit all source
files, headers, build metadata and `LICENSE.md`; generated `.lib` and `.obj`
files do not need to be committed. There is no GLFW runtime DLL to deploy.

The upstream CMake project is also retained for rebuilding GLFW independently
on other platforms. This does not make GSE itself cross-platform: the game
still uses Windows-specific font, console and file APIs.

GLFW uses the zlib/libpng license in `LICENSE.md`. GSE copies it into
`<output>/Licenses/GLFW/` on build.

Official references:

- https://www.glfw.org/docs/3.4/build_guide.html
- https://www.glfw.org/docs/3.4/input_guide.html
- https://www.glfw.org/docs/3.4/window_guide.html
