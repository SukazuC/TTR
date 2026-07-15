# Third-party notices

- `third_party/minhook`: MinHook v1.3.4, commit `c3fcafdc10146beb5919319d0683e44e3c30d537`, 2-clause BSD license. Its license is retained at `third_party/minhook/LICENSE.txt`.
- `third_party/nlohmann`: JSON for Modern C++ v3.12.0, MIT license. Used only by the developer-side manifest compiler. Its license is retained at `third_party/nlohmann/LICENSE.MIT`.
- `third_party/microsoft-ui-xaml`: Microsoft.UI.Xaml 2.8.7 WinMD used at build time to generate the WinUI 2 C++/WinRT projection. Its package license is retained at `third_party/microsoft-ui-xaml/LICENSE.txt`; no package DLL is redistributed by this project.
- `Microsoft.Web.WebView2` 1.0.2849.39: the official NuGet package is hash-pinned and downloaded into the build tree to supply metadata required while generating the Microsoft.UI.Xaml projection. It is not redistributed in project artifacts.
- `third_party/windhawk-reference/taskbar-thumbnail-reorder.wh.cpp`: current source snapshot from `ramensoftware/windhawk-mods`, commit `bc9c9d57104d5081e9e70a507664872a0d4378e4`, by Michael Maltsev (m417z), GPLv3. It is retained as the behavioral and symbol-name reference.

The complete project is distributed under GPL-3.0-only; see `LICENSE`.
