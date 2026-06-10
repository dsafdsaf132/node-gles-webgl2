# [node-gles-webgl2](https://www.npmjs.com/package/node-gles-webgl2)

Headless WebGL2 / OpenGL ES 3 runtime for Node.js backed by [ANGLE](https://github.com/google/angle).

This package is a fork of [`node-gles`](https://github.com/google/node-gles). It
preserves the existing WebGL1 surface and exposes additional GLES3-backed APIs
through WebGL2-compatible JavaScript method names.

## WebGL2 Coverage

| Feature | Status |
| --- | --- |
| Runtime and context lifecycle | ✅ |
| WebGL1 API and selected extension aliases | ✅ |
| WebGL2 core method names | ✅ |
| VAO, instancing, draw buffers, and multisample framebuffers | ✅ |
| Buffer, sampler, sync, and PBO operations | ✅ |
| WebGL2 uniform and matrix APIs | ✅ |
| 3D textures and 2D texture arrays | ✅ |
| Queries, transform feedback, and integer vertex attributes | ✅ |
| Browser-compatible overloads and error semantics | 🟡 |
| Browser-complete WebGL2 conformance | ❌ |

## Platform Support

| Platform      | CI                                                                 |
| ------------- | ------------------------------------------------------------------ |
| Linux x64     | ![tested](https://img.shields.io/badge/CI-tested-brightgreen)      |
| Linux arm64   | ![tested](https://img.shields.io/badge/CI-tested-brightgreen)      |
| macOS arm64   | ![tested](https://img.shields.io/badge/CI-tested-brightgreen)      |
| macOS x64     | ![build only](https://img.shields.io/badge/CI-build%20only-yellow) |
| Windows x64   | ![tested](https://img.shields.io/badge/CI-tested-brightgreen)      |
| Windows arm64 | ![tested](https://img.shields.io/badge/CI-tested-brightgreen)      |

macOS x64 is build-only in CI because hosted-runner smoke currently hits
`Error: No display`.

## Extension Support

`getSupportedExtensions()` reports WebGL extension names, not raw `GL_*` ANGLE
extension strings. Runtime availability still depends on the selected ANGLE
backend.

### Exposed

- `ANGLE_instanced_arrays`
- `EXT_blend_minmax`
- `EXT_color_buffer_float` / `WEBGL_color_buffer_float`
- `EXT_color_buffer_half_float`
- `EXT_frag_depth`
- `EXT_float_blend`
- `EXT_sRGB`
- `EXT_shader_texture_lod`
- `EXT_texture_filter_anisotropic`
- `EXT_texture_mirror_clamp_to_edge`
- `OES_element_index_uint`
- `OES_standard_derivatives`
- `OES_texture_float` / `OES_texture_float_linear`
- `OES_texture_half_float` / `OES_texture_half_float_linear`
- `OES_vertex_array_object`
- `WEBGL_compressed_texture_s3tc` / `WEBGL_compressed_texture_s3tc_srgb`
- `WEBGL_debug_renderer_info`
- `WEBGL_depth_texture`
- `WEBGL_draw_buffers`
- `WEBGL_lose_context`

### Not Exposed

- `EXT_disjoint_timer_query` / `EXT_disjoint_timer_query_webgl2`

## Install

From npm:

```sh
npm install node-gles-webgl2
```


## Usage

```js
const nodeGles = require("node-gles-webgl2");

const gl = nodeGles.createWebGLRenderingContext({
  width: 800,
  height: 500,
  majorVersion: 3,
  minorVersion: 0,
});

console.log(gl.getParameter(gl.VERSION));
```

### Context Options

- `width` / `height`: drawing buffer size, default `1`.
- `majorVersion` / `minorVersion`: requested OpenGL ES version, default `3.0`.
- `webGLCompatibility`: requests ANGLE WebGL compatibility mode. The legacy
  misspelled `webGLCompability` option is still accepted as an alias.
- `enabledExtensions`: optional WebGL extension allowlist. When set, only listed
  supported extensions are exposed.
- `disabledExtensions`: optional WebGL extension blocklist. This takes
  precedence over `enabledExtensions`.

## Context Lifecycle

For batch rendering, call `gl.destroy()` or `gl.dispose()` after the final
`readPixels()` for a context. This releases the native EGL context and pbuffer
surface immediately instead of waiting for JavaScript garbage collection.

```js
const gl = nodeGles.createWebGLRenderingContext({ width: 3000, height: 2000 });

try {
  // render and readPixels
} finally {
  gl.destroy();
}
```

## Build From Source

From git:

```sh
git clone https://github.com/dsafdsaf132/node-gles-webgl2.git
cd node-gles-webgl2
npm install --ignore-scripts
node scripts/install.js
npm run build
```

From a release source tarball, use the same commands after extracting the
archive. Source tarballs do not contain `deps/angle`; `node scripts/install.js`
downloads ANGLE into that directory before building the addon.

`scripts/install.js` downloads the latest matching ANGLE prebuilt archive from
[`dsafdsaf132/angle-prebuilt`](https://github.com/dsafdsaf132/angle-prebuilt)
into `deps/angle` and builds the native addon with `node-gyp`. If a complete
ANGLE `out/Release` directory is already present and matches the selected
release, the installer reuses it.

Installer overrides:

- `NODE_GLES_ANGLE_RELEASE_REPOSITORY`: GitHub release repository, default
  `dsafdsaf132/angle-prebuilt`
- `NODE_GLES_ANGLE_RELEASE_TAG`: GitHub release tag, default `latest`
- `NODE_GLES_ANGLE_VERSION` and `NODE_GLES_ANGLE_BASE_URI`: legacy explicit
  archive URL mode
- `NODE_GLES_ANGLE_SHA256`: optional archive checksum verification
- `NODE_GLES_GITHUB_TOKEN`: optional GitHub API token for release lookups

On Linux, the native build needs X11 development headers. On Ubuntu:

```sh
sudo apt-get install -y build-essential python3 libx11-dev libxext-dev
```

## License

This package is licensed under the Apache License, Version 2.0. See
[LICENSE](LICENSE) for the full license text.

`node-gles-webgl2` is a fork of
[`node-gles`](https://github.com/google/node-gles). Original source files retain
their upstream copyright notices, including Google LLC / Google Inc. notices
where applicable. Modifications in this repository add WebGL2 / OpenGL ES 3
bindings, packaging, CI, and publishing workflows for `node-gles-webgl2`.
