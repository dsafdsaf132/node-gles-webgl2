# node-gles-webgl2

`node-gles-webgl2` is a fork of `node-gles` focused on exposing missing
WebGL2 / OpenGL ES 3 APIs from the JavaScript WebGL context.

The upstream project already creates an ANGLE OpenGL ES runtime for headless
Node.js rendering. This fork keeps that behavior and surfaces GLES3 entry
points through browser-compatible WebGL2 method names.

## Scope

This fork keeps the existing WebGL1 API behavior and adds native GLES-backed
WebGL2 methods where ANGLE exposes the underlying entry points.

Core WebGL2 entry points:

```js
gl.createVertexArray();
gl.bindVertexArray(vaoOrNull);
gl.deleteVertexArray(vao);
gl.isVertexArray(vao);
gl.drawArraysInstanced(mode, first, count, instanceCount);
gl.drawElementsInstanced(mode, count, type, offset, instanceCount);
gl.vertexAttribDivisor(index, divisor);
```

Additional WebGL2 / GLES3 bindings include:

- draw/read buffers and framebuffer blit/invalidation
- multisample renderbuffer storage
- 3D texture upload, sub-upload, copy, and compressed upload
- indexed buffer binding and buffer copy
- query objects
- sampler objects
- sync objects
- transform feedback objects and varyings
- integer vertex attributes
- unsigned integer uniforms
- non-square matrix uniforms
- `getFragDataLocation()`
- `getIndexedParameter()`
- `getInternalformatParameter()`

## Object Handles

This repository preserves the original `node-gles` handle convention:

- WebGL object handles such as buffers, textures, VAOs, queries, samplers, and
  transform feedback objects are numeric GLES handles.
- `WebGLSync` values are wrapped native objects.
- Nullable WebGL arguments accept `null` and `undefined` where browser WebGL
  semantics allow binding or deleting "no object".

The exposed JavaScript method names and argument order follow WebGL2 names so
that wasm-bindgen generated browser glue can call this context directly.

## Creating a Context

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

When using the local checkout directly:

```js
const nodeGles = require("/path/to/node-gles-webgl2");
```

## Build

Install dependencies and build the native addon:

```sh
npm install --ignore-scripts
node scripts/install.js
```

If the system `node-gyp` is broken, build with a current local copy:

```sh
npx --yes node-gyp rebuild
```

`scripts/install.js` downloads the ANGLE binary package into `deps/angle` and
then runs `node-gyp rebuild`.

## Smoke Tests

Check that the WebGL2 methods are exposed:

```js
const nodeGles = require(".");
const gl = nodeGles.createWebGLRenderingContext({
  width: 8,
  height: 8,
  majorVersion: 3,
  minorVersion: 0
});

console.log(gl.getParameter(gl.VERSION));
console.log(typeof gl.createVertexArray);
console.log(typeof gl.bindVertexArray);
console.log(typeof gl.deleteVertexArray);
console.log(typeof gl.drawArraysInstanced);
console.log(typeof gl.drawElementsInstanced);
console.log(typeof gl.vertexAttribDivisor);

const vao = gl.createVertexArray();
gl.bindVertexArray(null);
gl.bindVertexArray(vao);
gl.deleteVertexArray(vao);
console.log(gl.getError());
```

Run the TypeScript and native addon checks:

```sh
./node_modules/.bin/tsc --pretty false
npx --yes node-gyp rebuild
```

## Notes

This fork is intentionally pragmatic. It preserves the existing WebGL1 surface
from `node-gles` and adds WebGL2/GLES3 APIs that can be bound directly to ANGLE
without JavaScript emulation.
