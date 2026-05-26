"use strict";

const assert = require("assert");
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

const gl = createContext();
console.log(gl.getParameter(gl.VERSION));
testRequiredWebGL2Methods(gl);
testWebGLOnlyPixelStore(gl);
testVertexArrayState(gl);
testInstancedDrawing(gl, false);
testInstancedDrawing(gl, true);
