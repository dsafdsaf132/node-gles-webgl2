"use strict";

const assert = require("assert");
const twgl = require("twgl.js");
const nodeGles = require("..");

const WIDTH = 32;
const HEIGHT = 32;

function createContext(width = WIDTH, height = HEIGHT) {
  return nodeGles.createWebGLRenderingContext({
    width,
    height,
    majorVersion: 3,
    minorVersion: 0
  });
}

function readPixel(gl, x, y) {
  const pixel = new Uint8Array(4);
  gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  return pixel;
}

function assertPixelNear(pixel, expected, label, tolerance = 4) {
  for (let i = 0; i < 4; ++i) {
    assert(
        Math.abs(pixel[i] - expected[i]) <= tolerance,
        `${label}: channel ${i} expected ${expected[i]}, got ${pixel[i]}`);
  }
}

function coloredRectanglesArrays() {
  return {
    position: {
      numComponents: 2,
      data: [
        -0.9, -0.7, -0.15, -0.7, -0.9, 0.7, -0.15, 0.7,
        0.15, -0.7, 0.9, -0.7, 0.15, 0.7, 0.9, 0.7
      ]
    },
    color: {
      numComponents: 4,
      data: [
        1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1
      ]
    },
    indices: [0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7]
  };
}

function testProgramBufferAndDrawHelpers() {
  const gl = createContext();
  try {
    const programInfo = twgl.createProgramInfo(gl, [`
attribute vec2 position;
attribute vec4 color;
varying vec4 v_color;
void main() {
  v_color = color;
  gl_Position = vec4(position, 0.0, 1.0);
}
`, `
precision mediump float;
uniform vec4 u_tint;
varying vec4 v_color;
void main() {
  gl_FragColor = v_color * u_tint;
}
`]);
    const bufferInfo =
        twgl.createBufferInfoFromArrays(gl, coloredRectanglesArrays());

    assert.strictEqual(typeof programInfo.uniformSetters.u_tint, "function");

    gl.viewport(0, 0, WIDTH, HEIGHT);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.useProgram(programInfo.program);

    twgl.setBuffersAndAttributes(gl, programInfo, bufferInfo);
    twgl.setUniforms(programInfo, {u_tint: [1, 1, 1, 1]});
    twgl.drawBufferInfo(gl, bufferInfo);

    assertPixelNear(readPixel(gl, 8, 16), [255, 0, 0, 255], "left rectangle");
    assertPixelNear(readPixel(gl, 24, 16), [0, 255, 0, 255],
                    "right rectangle");
    assertPixelNear(readPixel(gl, 16, 16), [0, 0, 0, 255],
                    "rectangle gap");
    assert.strictEqual(gl.getError(), gl.NO_ERROR, "default draw GL error");
  } finally {
    gl.destroy();
  }
}

testProgramBufferAndDrawHelpers();
