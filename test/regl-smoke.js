"use strict";

const assert = require("assert");
const createREGL = require("regl");
const nodeGles = require("..");

const WIDTH = 32;
const HEIGHT = 32;

function createContext() {
  return nodeGles.createWebGLRenderingContext({
    width: WIDTH,
    height: HEIGHT,
    majorVersion: 3,
    minorVersion: 0
  });
}

function assertPixelNear(pixel, expected, label, tolerance = 4) {
  for (let i = 0; i < 4; ++i) {
    assert(
        Math.abs(pixel[i] - expected[i]) <= tolerance,
        `${label}: channel ${i} expected ${expected[i]}, got ${pixel[i]}`);
  }
}

function readDefaultPixel(gl, x, y) {
  const pixel = new Uint8Array(4);
  gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  return pixel;
}

function withRegl(label, options, body) {
  const gl = createContext();
  let regl = null;
  try {
    regl = createREGL(Object.assign({gl}, options));
    body(regl, gl);
    assert.strictEqual(gl.getError(), gl.NO_ERROR, `${label}: GL error`);

    const reglToDestroy = regl;
    regl = null;
    reglToDestroy.destroy();
    assert.strictEqual(
        gl.getError(), gl.NO_ERROR, `${label}: GL error after destroy`);
  } finally {
    if (regl !== null) {
      regl.destroy();
    }
    gl.destroy();
  }
}

function testBasicDraw() {
  withRegl("basic draw", {}, (regl, gl) => {
    const draw = regl({
      vert: `
precision mediump float;
attribute vec2 position;
void main() {
  gl_Position = vec4(position, 0.0, 1.0);
}
`,
      frag: `
precision mediump float;
void main() {
  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
`,
      attributes: {
        position: [[-1, -1], [1, -1], [-1, 1]]
      },
      count: 3
    });

    regl.clear({color: [1, 0, 0, 1], depth: 1});
    draw();

    assertPixelNear(readDefaultPixel(gl, 8, 8), [0, 255, 0, 255],
                    "basic draw");
  });
}

function testTextureFramebufferAndElements() {
  withRegl("texture framebuffer elements", {}, (regl) => {
    const texture = regl.texture({
      width: 2,
      height: 2,
      min: "nearest",
      mag: "nearest",
      data: new Uint8Array([
        0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255
      ])
    });
    const framebuffer = regl.framebuffer({
      width: 16,
      height: 16,
      colorFormat: "rgba",
      colorType: "uint8",
      depthStencil: false
    });

    const draw = regl({
      framebuffer,
      vert: `
precision mediump float;
attribute vec2 position;
varying vec2 v_uv;
void main() {
  v_uv = 0.5 * (position + 1.0);
  gl_Position = vec4(position, 0.0, 1.0);
}
`,
      frag: `
precision mediump float;
uniform sampler2D tex;
varying vec2 v_uv;
void main() {
  gl_FragColor = texture2D(tex, v_uv);
}
`,
      attributes: {
        position: [[-1, -1], [1, -1], [-1, 1], [1, 1]]
      },
      elements: [[0, 1, 2], [2, 1, 3]],
      uniforms: {
        tex: texture
      }
    });

    regl.clear({framebuffer, color: [1, 0, 0, 1]});
    draw();

    const pixel = regl.read({framebuffer, x: 8, y: 8, width: 1, height: 1});
    assertPixelNear(pixel, [0, 0, 255, 255],
                    "texture framebuffer elements");
  });
}

function testInstancedAttributes() {
  withRegl("instanced attributes", {extensions: ["angle_instanced_arrays"]},
           (regl, gl) => {
             const offsets = regl.buffer([[-0.5, 0.0], [0.5, 0.0]]);
             const draw = regl({
               vert: `
precision mediump float;
attribute vec2 position;
attribute vec2 offset;
void main() {
  gl_PointSize = 5.0;
  gl_Position = vec4(position + offset, 0.0, 1.0);
}
`,
               frag: `
precision mediump float;
void main() {
  gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0);
}
`,
               attributes: {
                 position: [[0, 0]],
                 offset: {
                   buffer: offsets,
                   divisor: 1
                 }
               },
               primitive: "points",
               count: 1,
               instances: 2
             });

             regl.clear({color: [0, 0, 0, 1], depth: 1});
             draw();

             assertPixelNear(readDefaultPixel(gl, 8, 16), [255, 255, 0, 255],
                             "left instance");
             assertPixelNear(readDefaultPixel(gl, 24, 16), [255, 255, 0, 255],
                             "right instance");
           });
}

function testRepeatedLifecycle() {
  for (let i = 0; i < 5; ++i) {
    withRegl(`lifecycle ${i}`, {}, (regl, gl) => {
      const draw = regl({
        vert: `
precision mediump float;
attribute vec2 position;
void main() {
  gl_Position = vec4(position, 0.0, 1.0);
}
`,
        frag: `
precision mediump float;
void main() {
  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
`,
        attributes: {
          position: [[-1, -1], [1, -1], [-1, 1]]
        },
        count: 3
      });

      regl.clear({color: [1, 0, 0, 1], depth: 1});
      draw();
      assertPixelNear(readDefaultPixel(gl, 8, 8), [0, 255, 0, 255],
                      `lifecycle ${i}`);
    });
  }
}

testBasicDraw();
testTextureFramebufferAndElements();
testInstancedAttributes();
testRepeatedLifecycle();
