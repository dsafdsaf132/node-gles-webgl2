"use strict";

const assert = require("assert");
const {execFileSync} = require("child_process");
const path = require("path");
const nodeGles = require("..");

function createContext() {
  return nodeGles.createWebGLRenderingContext({
    width: 16,
    height: 16,
    majorVersion: 3,
    minorVersion: 0
  });
}

function assertNoError(gl, label) {
  const error = gl.getError();
  assert.strictEqual(error, gl.NO_ERROR, `${label}: GL error ${error}`);
}

function compileShader(gl, type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  assert.strictEqual(
      gl.getShaderParameter(shader, gl.COMPILE_STATUS), true,
      gl.getShaderInfoLog(shader));
  return shader;
}

function createProgram(gl) {
  const vertexShader = compileShader(gl, gl.VERTEX_SHADER, `#version 300 es
precision highp float;
in vec2 a_position;
in vec2 a_offset;
void main() {
  gl_PointSize = 4.0;
  gl_Position = vec4(a_position + a_offset, 0.0, 1.0);
}
`);
  const fragmentShader = compileShader(gl, gl.FRAGMENT_SHADER, `#version 300 es
precision highp float;
out vec4 out_color;
void main() {
  out_color = vec4(1.0, 0.0, 0.0, 1.0);
}
`);

  const program = gl.createProgram();
  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);
  assert.strictEqual(
      gl.getProgramParameter(program, gl.LINK_STATUS), true,
      gl.getProgramInfoLog(program));
  return program;
}

function assertRedPixel(gl, x, y, label) {
  const pixel = new Uint8Array(4);
  gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  assert(pixel[0] > 200, `${label}: expected red channel, got ${pixel[0]}`);
  assert(pixel[1] < 40, `${label}: expected low green, got ${pixel[1]}`);
  assert(pixel[2] < 40, `${label}: expected low blue, got ${pixel[2]}`);
  assert(pixel[3] > 200, `${label}: expected alpha, got ${pixel[3]}`);
}

function assertPixelNear(pixel, expected, label, tolerance = 2) {
  for (let i = 0; i < 4; ++i) {
    assert(
        Math.abs(pixel[i] - expected[i]) <= tolerance,
        `${label}: channel ${i} expected ${expected[i]}, got ${pixel[i]}`);
  }
}

function readTexture2DPixel(gl, texture, x, y) {
  const framebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
  gl.framebufferTexture2D(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
  const pixel = new Uint8Array(4);
  gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.deleteFramebuffer(framebuffer);
  return pixel;
}

function readTextureLayerPixel(gl, texture, layer, x, y) {
  const framebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
  gl.framebufferTextureLayer(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, texture, 0, layer);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
  const pixel = new Uint8Array(4);
  gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.deleteFramebuffer(framebuffer);
  return pixel;
}

function configureTexture(gl, target) {
  gl.texParameteri(target, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(target, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(target, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(target, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}

function testRequiredWebGL2Methods(gl) {
  const required = [
    "createVertexArray",
    "bindVertexArray",
    "deleteVertexArray",
    "isVertexArray",
    "drawArraysInstanced",
    "drawElementsInstanced",
    "vertexAttribDivisor"
  ];
  for (const name of required) {
    assert.strictEqual(typeof gl[name], "function", `${name} is missing`);
  }
}

function testWebGLOnlyPixelStore(gl) {
  assert.strictEqual(gl.getParameter(gl.UNPACK_FLIP_Y_WEBGL), false);
  assert.strictEqual(gl.getParameter(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL), false);
  assert.strictEqual(
      gl.getParameter(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL),
      gl.BROWSER_DEFAULT_WEBGL);

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, true);
  gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
  assert.strictEqual(gl.getParameter(gl.UNPACK_FLIP_Y_WEBGL), true);
  assert.strictEqual(gl.getParameter(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL), true);
  assert.strictEqual(gl.getParameter(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL),
                     gl.NONE);
  assertNoError(gl, "WebGL-only pixelStorei");

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL,
                 gl.BROWSER_DEFAULT_WEBGL);
  assert.strictEqual(gl.getParameter(gl.UNPACK_FLIP_Y_WEBGL), false);
  assert.strictEqual(gl.getParameter(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL), false);
  assert.strictEqual(
      gl.getParameter(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL),
      gl.BROWSER_DEFAULT_WEBGL);

  gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, 1234);
  gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, 5678);
  assert.strictEqual(gl.getError(), gl.INVALID_VALUE);
  assert.strictEqual(gl.getError(), gl.INVALID_VALUE);
  assert.strictEqual(
      gl.getParameter(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL),
      gl.BROWSER_DEFAULT_WEBGL);

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 2);
  assert.strictEqual(gl.getError(), gl.NO_ERROR);
  assert.strictEqual(gl.getParameter(gl.UNPACK_FLIP_Y_WEBGL), true);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);
  assert.strictEqual(gl.getParameter(gl.UNPACK_FLIP_Y_WEBGL), false);

  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 2);
  assert.strictEqual(gl.getError(), gl.NO_ERROR);
  assert.strictEqual(gl.getParameter(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL), true);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 0);
  assert.strictEqual(gl.getParameter(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL), false);
}

function testWebGLUnpackTransforms(gl) {
  const pixels2D = new Uint8Array([
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 255, 255
  ]);
  const texture2D = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture2D);
  configureTexture(gl, gl.TEXTURE_2D);

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, pixels2D);
  assertPixelNear(
      readTexture2DPixel(gl, texture2D, 0, 0), [0, 0, 255, 255],
      "texImage2D UNPACK_FLIP_Y_WEBGL");
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);
  gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL,
                 gl.BROWSER_DEFAULT_WEBGL);

  const objectTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, objectTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, {
    width: 2,
    height: 2,
    data: pixels2D
  });
  assertPixelNear(
      readTexture2DPixel(gl, objectTexture, 0, 0), [0, 0, 255, 255],
      "texImage2D object source UNPACK_FLIP_Y_WEBGL");
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);

  const pboTexture2D = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, pboTexture2D);
  configureTexture(gl, gl.TEXTURE_2D);
  const pbo2D = gl.createBuffer();
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, pbo2D);
  gl.bufferData(gl.PIXEL_UNPACK_BUFFER, pixels2D, gl.STATIC_DRAW);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, 0);
  assertPixelNear(
      readTexture2DPixel(gl, pboTexture2D, 0, 0), [0, 0, 255, 255],
      "texImage2D PBO UNPACK_FLIP_Y_WEBGL");
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);

  const premultiplyTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, premultiplyTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 1);
  gl.texImage2D(
      gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
      new Uint8Array([100, 50, 25, 128]));
  assertPixelNear(
      readTexture2DPixel(gl, premultiplyTexture, 0, 0),
      [50, 25, 13, 128], "texImage2D UNPACK_PREMULTIPLY_ALPHA_WEBGL");
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 0);

  const subTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, subTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, null);
  const paddedRows = new Uint8Array([
    9, 9, 9, 9, 255, 0, 0, 255, 0, 255, 0, 255,
    9, 9, 9, 9, 0, 0, 255, 255, 255, 255, 255, 255
  ]);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 3);
  gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 1);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 2, 2, gl.RGBA,
                   gl.UNSIGNED_BYTE, paddedRows);
  assertPixelNear(
      readTexture2DPixel(gl, subTexture, 0, 0), [0, 0, 255, 255],
      "texSubImage2D flip with unpack row length");
  assert.strictEqual(gl.getParameter(gl.UNPACK_ROW_LENGTH), 3);
  assert.strictEqual(gl.getParameter(gl.UNPACK_SKIP_PIXELS), 1);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
  gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 0);

  const texture3D = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_3D, texture3D);
  configureTexture(gl, gl.TEXTURE_3D);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.texImage3D(gl.TEXTURE_3D, 0, gl.RGBA, 2, 2, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, pixels2D);
  assertPixelNear(
      readTextureLayerPixel(gl, texture3D, 0, 0, 0), [0, 0, 255, 255],
      "texImage3D UNPACK_FLIP_Y_WEBGL");
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);

  const textureArray = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D_ARRAY, textureArray);
  configureTexture(gl, gl.TEXTURE_2D_ARRAY);
  gl.texImage3D(gl.TEXTURE_2D_ARRAY, 0, gl.RGBA, 2, 2, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, null);
  const pbo = gl.createBuffer();
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, pbo);
  gl.bufferData(gl.PIXEL_UNPACK_BUFFER, pixels2D, gl.STATIC_DRAW);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.texSubImage3D(gl.TEXTURE_2D_ARRAY, 0, 0, 0, 0, 2, 2, 1, gl.RGBA,
                   gl.UNSIGNED_BYTE, 0);
  assertPixelNear(
      readTextureLayerPixel(gl, textureArray, 0, 0, 0), [0, 0, 255, 255],
      "texSubImage3D PBO UNPACK_FLIP_Y_WEBGL");
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
  assertNoError(gl, "WebGL unpack transform uploads");
}

function testVertexArrayState(gl) {
  const vao = gl.createVertexArray();
  const buffer = gl.createBuffer();
  gl.bindVertexArray(null);
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0]), gl.STATIC_DRAW);
  gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
  gl.enableVertexAttribArray(0);
  assert.strictEqual(gl.getParameter(gl.VERTEX_ARRAY_BINDING), vao);
  gl.bindVertexArray(null);
  assert.strictEqual(gl.getParameter(gl.VERTEX_ARRAY_BINDING), 0);
  gl.bindVertexArray(vao);
  gl.deleteVertexArray(vao);
  assertNoError(gl, "VAO state smoke");
}

function testInstancedDrawing(gl, indexed) {
  const program = createProgram(gl);
  gl.useProgram(program);
  gl.viewport(0, 0, 16, 16);
  gl.clearColor(0, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);

  const vao = gl.createVertexArray();
  gl.bindVertexArray(vao);

  const positionLocation = gl.getAttribLocation(program, "a_position");
  const offsetLocation = gl.getAttribLocation(program, "a_offset");
  assert(positionLocation >= 0, "a_position location not found");
  assert(offsetLocation >= 0, "a_offset location not found");

  const positionBuffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0]), gl.STATIC_DRAW);
  gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);
  gl.enableVertexAttribArray(positionLocation);

  const offsetBuffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, offsetBuffer);
  gl.bufferData(
      gl.ARRAY_BUFFER,
      new Float32Array([-0.5, -0.5, 0.5, 0.5]),
      gl.STATIC_DRAW);
  gl.vertexAttribPointer(offsetLocation, 2, gl.FLOAT, false, 0, 0);
  gl.enableVertexAttribArray(offsetLocation);
  gl.vertexAttribDivisor(offsetLocation, 1);

  if (indexed) {
    const elementBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, elementBuffer);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0]),
                  gl.STATIC_DRAW);
    gl.drawElementsInstanced(gl.POINTS, 1, gl.UNSIGNED_SHORT, 0, 2);
  } else {
    gl.drawArraysInstanced(gl.POINTS, 0, 1, 2);
  }

  assertRedPixel(gl, 4, 4, indexed ? "drawElementsInstanced A" :
                                     "drawArraysInstanced A");
  assertRedPixel(gl, 12, 12, indexed ? "drawElementsInstanced B" :
                                      "drawArraysInstanced B");
  assertNoError(gl, indexed ? "drawElementsInstanced" :
                              "drawArraysInstanced");
}

function testWebGL2PixelStoreIsGatedForES2() {
  execFileSync(process.execPath, ["-e", `
    const assert = require("assert");
    const nodeGles = require(".");
    const gl = nodeGles.createWebGLRenderingContext({
      width: 1,
      height: 1,
      majorVersion: 2,
      minorVersion: 0
    });
    const version = gl.getParameter(gl.VERSION);
    if (/OpenGL ES 2\\./.test(version)) {
      gl.getError();
      gl.getParameter(gl.UNPACK_ROW_LENGTH);
      const error = gl.getError();
      assert(error === gl.NO_ERROR || error === gl.INVALID_ENUM,
             "unexpected ES2 pixel-store query error " + error);
    }
  `], {cwd: path.resolve(__dirname, ".."), stdio: "pipe"});
}

const gl = createContext();
console.log(gl.getParameter(gl.VERSION));
testRequiredWebGL2Methods(gl);
testWebGLOnlyPixelStore(gl);
testWebGLUnpackTransforms(gl);
testVertexArrayState(gl);
testInstancedDrawing(gl, false);
testInstancedDrawing(gl, true);
testWebGL2PixelStoreIsGatedForES2();
