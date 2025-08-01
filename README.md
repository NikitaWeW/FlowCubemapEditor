Introduction
===
A flow map editor, but on a cube. Inspired by this [flow map painter](https://teckartist.com/?page_id=107).

Build
===

uses cmake:
``` shell
cmake -S . -B build
cmake --build build
cmake --install build --prefix build/install
```

HDR flowmaps
===

There is an option to bake the flowmap in HDR, which means values won’t be clamped to 1. **HDR flowmaps may look strange at first.** Most image formats don’t support negative values, so to work around this, I stored the sign in the blue and alpha channels (1 indicates a negative sign, 0 indicates positive). To retrieve the true flow value, use the following:

```
    red   = blue  ? -red : red;
    green = alpha ? -green : green;

```

---

If something is unclear or you found a bug, please create a github issue or a pull request. Thanks!
