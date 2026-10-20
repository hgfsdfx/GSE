# Asset loader dependencies

Vendored headers are pinned; the build does not download dependencies.

| Component | Version | License | Upstream |
| --- | --- | --- | --- |
| cgltf | v1.15 | MIT, see `cgltf.LICENSE` | https://github.com/jkuhlmann/cgltf/tree/v1.15 |
| JSON for Modern C++ | v3.12.0 | MIT, see `json.LICENSE.MIT` | https://github.com/nlohmann/json/tree/v3.12.0 |

Downloaded official files:

- https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.15/cgltf.h
- https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.15/LICENSE
- https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
- https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT

SHA-256 (original vendored bytes):

```text
cgltf.h  E378A21C084BF1F288BB799DE827BB26906EFB024255F1ECF1705EA13F11C6EC
json.hpp AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63
```

Format reference: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html

Keep these license files with redistributed source and binary distributions.
The current renderer implements a subset of glTF rendering; see the loader guide for limitations.
