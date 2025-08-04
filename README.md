Introduction
===
A flow map editor, but on a cube. Inspired by this [flow map painter](https://teckartist.com/?page_id=107). This is mostly a 1500 line mess, but I think its fine for such a small project like this.

Build
===

uses cmake:
``` shell
cmake -S . -B build
cmake --build build
cmake --install build --prefix build/install
```

Usage
===

the unwrapped cube layout is the following:
```
+----+----+----+
| X+ | Y+ | Z+ |
+----+----+----+
| X- | Y- | Z- |
+----+----+----+
```

When using the 6 images save / load layout, square images are expected to save / load with the following names (any supported extension):

pos_x, neg_x, pos_y, neg_y, pos_z, neg_z

HDR flowmaps
===

There is an option to bake the flowmap in HDR, which means values won’t be clamped to 1. **HDR flowmaps may look strange at first.** Most image formats don’t support negative values, so to work around this, I stored the sign in the blue and alpha channels (1 indicates a negative sign, 0 indicates positive). To retrieve the true flow value, use the following:

```
    red   = blue  ? -red : red;
    green = alpha ? -green : green;
```

---

If something is unclear or you found a bug, please create a github issue or a pull request. Thanks!
