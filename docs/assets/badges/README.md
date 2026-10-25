# C++ requirement badges

![C++17 or later](cpp17.svg) ![C++20 or later](cpp20.svg) ![C++23 or later](cpp23.svg) ![C++26 or later](cpp26.svg)

Each badge expresses the minimum language standard required by an individual
method. The SVGs are 86 × 22 pixels, scale cleanly, and have a transparent exterior
for light and dark backgrounds. They use local font fallbacks without network
requests.

In the repository's main README, use:

```markdown
![C++23 or later](docs/assets/badges/cpp23.svg)
```

The Doxygen configuration provides `\cpp17`, `\cpp20`, `\cpp23`, and `\cpp26`
aliases for inline badges in generated HTML. For example:

```cpp
/** \cpp23
 * Convert the field to an expected value.
 */
```

Regenerate the SVGs from the repository root with:

```sh
python tools/generate_cpp_badges.py
```
