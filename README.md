# node-gles-webgl2

Headless WebGL2 / OpenGL ES 3 runtime for Node.js backed by ANGLE.

This package is a fork of `node-gles`. It preserves the existing WebGL1 surface
and exposes additional GLES3-backed APIs through WebGL2-compatible JavaScript
method names.

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

## Build From Source

```sh
npm install --ignore-scripts
node scripts/install.js
npm run build
```

`scripts/install.js` downloads the ANGLE binary package into `deps/angle` and
builds the native addon with `node-gyp`.

On Linux, the native build needs X11 development headers. On Ubuntu:

```sh
sudo apt-get install -y build-essential python3 libx11-dev
```
