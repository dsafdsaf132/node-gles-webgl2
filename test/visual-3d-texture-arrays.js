"use strict";

const assert = require("assert");
const fs = require("fs");
const os = require("os");
const path = require("path");
const zlib = require("zlib");
const nodeGles = require("..");

const WIDTH = 768;
const HEIGHT = 512;
const TEXTURE_SIZE = 4;
const LAYERS = 3;

function makeCrcTable() {
  const table = new Uint32Array(256);
  for (let i = 0; i < table.length; ++i) {
    let c = i;
    for (let k = 0; k < 8; ++k) {
      c = (c & 1) ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    }
    table[i] = c >>> 0;
  }
  return table;
}

const CRC_TABLE = makeCrcTable();

function crc32(buffer) {
  let c = 0xffffffff;
  for (let i = 0; i < buffer.length; ++i) {
    c = CRC_TABLE[(c ^ buffer[i]) & 0xff] ^ (c >>> 8);
  }
  return (c ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const typeBuffer = Buffer.from(type, "ascii");
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length, 0);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([typeBuffer, data])), 0);
  return Buffer.concat([length, typeBuffer, data, crc]);
}

function writePng(filePath, width, height, rgbaBottomUp) {
  const stride = width * 4;
  const raw = Buffer.alloc((stride + 1) * height);
  for (let y = 0; y < height; ++y) {
    const dst = y * (stride + 1);
    raw[dst] = 0;
    const src = (height - 1 - y) * stride;
    rgbaBottomUp.copy(raw, dst + 1, src, src + stride);
  }

  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 6;
  header[10] = 0;
  header[11] = 0;
  header[12] = 0;

  const png = Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    pngChunk("IHDR", header),
    pngChunk("IDAT", zlib.deflateSync(raw)),
    pngChunk("IEND", Buffer.alloc(0))
  ]);
  fs.mkdirSync(path.dirname(filePath), {recursive: true});
  fs.writeFileSync(filePath, png);
}

function createShader(gl, type, source) {
  const shader = gl.createShader(type);
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  assert.strictEqual(
      gl.getShaderParameter(shader, gl.COMPILE_STATUS), true,
      gl.getShaderInfoLog(shader));
  return shader;
}

function createProgram(gl) {
  const vertexShader = createShader(gl, gl.VERTEX_SHADER, `#version 300 es
in vec2 a_position;
out vec2 v_uv;
void main() {
  v_uv = a_position * 0.5 + 0.5;
  gl_Position = vec4(a_position, 0.0, 1.0);
}
`);
  const fragmentShader = createShader(gl, gl.FRAGMENT_SHADER, `#version 300 es
precision highp float;
precision highp sampler3D;
precision highp sampler2DArray;

uniform sampler3D u_texture3d;
uniform sampler2DArray u_texture_array;
uniform int u_mode;
uniform float u_layer;

in vec2 v_uv;
out vec4 out_color;

void main() {
  vec2 cell = fract(v_uv * 4.0);
  vec4 border = vec4(0.02, 0.02, 0.02, 1.0);
  if (cell.x < 0.035 || cell.y < 0.035) {
    out_color = border;
    return;
  }

  if (u_mode == 0) {
    float z = (u_layer + 0.5) / 3.0;
    out_color = texture(u_texture3d, vec3(v_uv, z));
  } else {
    out_color = texture(u_texture_array, vec3(v_uv, u_layer));
  }
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

function textureData(kind) {
  const data = new Uint8Array(TEXTURE_SIZE * TEXTURE_SIZE * LAYERS * 4);
  for (let layer = 0; layer < LAYERS; ++layer) {
    for (let y = 0; y < TEXTURE_SIZE; ++y) {
      for (let x = 0; x < TEXTURE_SIZE; ++x) {
        const i = (((layer * TEXTURE_SIZE + y) * TEXTURE_SIZE + x) * 4);
        const xRamp = 48 + x * 52;
        const yRamp = 48 + y * 52;
        if (kind === "3d") {
          data[i + 0] = layer === 0 ? 255 : xRamp;
          data[i + 1] = layer === 1 ? 255 : yRamp;
          data[i + 2] = layer === 2 ? 255 : (x + y) % 2 ? 40 : 180;
        } else {
          data[i + 0] = layer === 0 ? 255 : yRamp;
          data[i + 1] = layer === 1 ? 255 : xRamp;
          data[i + 2] = layer === 2 ? 255 : 220 - x * 36;
        }
        data[i + 3] = 255;
      }
    }
  }
  return data;
}

function configureTexture(gl, target) {
  gl.texParameteri(target, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(target, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(target, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(target, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.texParameteri(target, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
}

function create3DTexture(gl) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_3D, texture);
  configureTexture(gl, gl.TEXTURE_3D);
  gl.texImage3D(
      gl.TEXTURE_3D, 0, gl.RGBA8, TEXTURE_SIZE, TEXTURE_SIZE, LAYERS, 0,
      gl.RGBA, gl.UNSIGNED_BYTE, textureData("3d"));
  return texture;
}

function create2DArrayTexture(gl) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D_ARRAY, texture);
  configureTexture(gl, gl.TEXTURE_2D_ARRAY);
  gl.texStorage3D(
      gl.TEXTURE_2D_ARRAY, 1, gl.RGBA8, TEXTURE_SIZE, TEXTURE_SIZE, LAYERS);
  gl.texSubImage3D(
      gl.TEXTURE_2D_ARRAY, 0, 0, 0, 0, TEXTURE_SIZE, TEXTURE_SIZE, LAYERS,
      gl.RGBA, gl.UNSIGNED_BYTE, textureData("array"));
  return texture;
}

function samplePixel(gl, x, y) {
  const pixel = new Uint8Array(4);
  gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  return Array.from(pixel);
}

function main() {
  const outputPath = process.argv[2] ||
      path.join(os.tmpdir(), "node-gles-webgl2-3d-texture-arrays.png");
  const gl = nodeGles.createWebGLRenderingContext({
    width: WIDTH,
    height: HEIGHT,
    majorVersion: 3,
    minorVersion: 0
  });

  try {
    const program = createProgram(gl);
    const positionLocation = gl.getAttribLocation(program, "a_position");
    const positionBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.bufferData(
        gl.ARRAY_BUFFER,
        new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
        gl.STATIC_DRAW);

    const texture3D = create3DTexture(gl);
    const textureArray = create2DArrayTexture(gl);
    assert.strictEqual(gl.getError(), gl.NO_ERROR);

    gl.useProgram(program);
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);
    gl.enableVertexAttribArray(positionLocation);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_3D, texture3D);
    gl.uniform1i(gl.getUniformLocation(program, "u_texture3d"), 0);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D_ARRAY, textureArray);
    gl.uniform1i(gl.getUniformLocation(program, "u_texture_array"), 1);

    gl.clearColor(0.08, 0.08, 0.08, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT);

    const panelWidth = WIDTH / 3;
    const panelHeight = HEIGHT / 2;
    for (let row = 0; row < 2; ++row) {
      for (let layer = 0; layer < LAYERS; ++layer) {
        gl.viewport(
            layer * panelWidth + 8, row * panelHeight + 8,
            panelWidth - 16, panelHeight - 16);
        gl.uniform1i(gl.getUniformLocation(program, "u_mode"),
                     row === 1 ? 0 : 1);
        gl.uniform1f(gl.getUniformLocation(program, "u_layer"), layer);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      }
    }

    assert.deepStrictEqual(samplePixel(gl, 32, 32)[3], 255);
    assert.deepStrictEqual(samplePixel(gl, 32, HEIGHT - 32)[3], 255);
    assert.strictEqual(gl.getError(), gl.NO_ERROR);

    const pixels = Buffer.alloc(WIDTH * HEIGHT * 4);
    gl.readPixels(0, 0, WIDTH, HEIGHT, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    writePng(outputPath, WIDTH, HEIGHT, pixels);
    console.log(outputPath);
  } finally {
    gl.destroy();
  }
}

main();
