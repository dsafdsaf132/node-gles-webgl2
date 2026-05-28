# [node-gles-webgl2](https://www.npmjs.com/package/node-gles-webgl2)

Headless WebGL2 / OpenGL ES 3 runtime for Node.js backed by ANGLE.

This package is a fork of [`node-gles`](https://github.com/google/node-gles). It
preserves the existing WebGL1 surface and exposes additional GLES3-backed APIs
through WebGL2-compatible JavaScript method names.

## Install

From npm:

```sh
npm install node-gles-webgl2
```

From GitHub Packages:

```sh
npm install @dsafdsaf132/node-gles-webgl2 --registry=https://npm.pkg.github.com
```

GitHub Packages installs require npm authentication for `npm.pkg.github.com`.

## Usage

```js
const nodeGles = require("node-gles-webgl2");

const gl = nodeGles.createWebGLRenderingContext({
  width: 800,
  height: 500,
  majorVersion: 3,
  minorVersion: 0
});

console.log(gl.getParameter(gl.VERSION));
```

## WebGL2 Coverage

The binding exposes the WebGL2 entry points used by wasm-bindgen renderers,
including VAOs, instanced rendering, draw buffers, multisample framebuffer
operations, buffer copy/readback, transform feedback, queries, samplers, sync
objects, 3D textures, integer vertex attributes, unsigned uniforms, and
non-square matrix uniforms.

Compatibility aliases are also available for common WebGL1 extension fallback
paths:

- `ANGLE_instanced_arrays`
- `OES_vertex_array_object`
- `WEBGL_draw_buffers`

Texture upload and pixel readback paths support WebGL2 typed-array offsets and
pixel buffer object numeric offsets where GLES3 supports them.

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

```sh
git clone https://github.com/dsafdsaf132/node-gles-webgl2.git
cd node-gles-webgl2
npm install --ignore-scripts
node scripts/install.js
npm run build
```

`scripts/install.js` downloads the ANGLE binary package into `deps/angle` and
builds the native addon with `node-gyp`. If a complete ANGLE `out/Release`
directory is already present, the installer reuses it.

Installer overrides:

- `NODE_GLES_ANGLE_VERSION`: ANGLE binary version, default `3729`
- `NODE_GLES_ANGLE_BASE_URI`: archive base URL
- `NODE_GLES_ANGLE_SHA256`: optional archive checksum verification

On Linux, the native build needs X11 development headers. On Ubuntu:

```sh
sudo apt-get install -y build-essential python3 libx11-dev
```

## Platform Support

| Platform | Status |
| --- | --- |
| Linux x64 | CI-tested. Requires X11 development headers for native builds. |
| macOS x64 | Installer path exists, but CI does not currently cover it. |
| Windows x64 | Installer path exists and copies ANGLE runtime DLLs next to the native addon after build. CI does not currently cover it. |

## License

This package is licensed under the Apache License, Version 2.0. See
[LICENSE](LICENSE) for the full license text.

`node-gles-webgl2` is a fork of
[`node-gles`](https://github.com/google/node-gles). Original source files retain
their upstream copyright notices, including Google LLC / Google Inc. notices
where applicable. Modifications in this repository add WebGL2 / OpenGL ES 3
bindings, packaging, CI, and publishing workflows for `node-gles-webgl2`.
