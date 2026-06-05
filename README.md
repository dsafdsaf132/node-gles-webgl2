# [node-gles-webgl2](https://www.npmjs.com/package/node-gles-webgl2)

Headless WebGL2 / OpenGL ES 3 runtime for Node.js backed by ANGLE.

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
| Queries, transform feedback, and integer vertex attributes | 🟡 |
| Browser-compatible overloads and error semantics | 🟡 |
| Browser-complete WebGL2 conformance | ❌ |

## Platform Support

| Platform      | Status | Notes                                                                                                |
| ------------- | ------ | ---------------------------------------------------------------------------------------------------- |
| Linux x64     | ✅     | CI-tested with the default ANGLE prebuilt archive. Requires X11 development headers for native builds. |
| Linux arm64   | ✅     | CI-tested with the default ANGLE prebuilt archive. Requires X11 development headers for native builds. |
| macOS arm64   | ✅     | CI-tested with the default ANGLE prebuilt archive.                                                    |
| macOS x64     | ❌     | Not supported by the default ANGLE prebuilt archive set.                                              |
| Windows x64   | ✅     | CI-tested with the default ANGLE prebuilt archive and copied ANGLE runtime DLLs.                      |
| Windows arm64 | ✅     | CI-tested with the default ANGLE prebuilt archive and copied ANGLE runtime DLLs.  

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
```                    |

## License

This package is licensed under the Apache License, Version 2.0. See
[LICENSE](LICENSE) for the full license text.

`node-gles-webgl2` is a fork of
[`node-gles`](https://github.com/google/node-gles). Original source files retain
their upstream copyright notices, including Google LLC / Google Inc. notices
where applicable. Modifications in this repository add WebGL2 / OpenGL ES 3
bindings, packaging, CI, and publishing workflows for `node-gles-webgl2`.
