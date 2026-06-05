"use strict";

const assert = require("assert");
const {execFileSync, spawnSync} = require("child_process");
const path = require("path");
const nodeGles = require("..");

function createContext(options = {}) {
  return nodeGles.createWebGLRenderingContext({
    width: options.width === undefined ? 16 : options.width,
    height: options.height === undefined ? 16 : options.height,
    majorVersion: options.majorVersion === undefined ? 3 : options.majorVersion,
    minorVersion: options.minorVersion === undefined ? 0 : options.minorVersion
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

function createProgramFromSources(gl, vertexSource, fragmentSource) {
  const vertexShader = compileShader(gl, gl.VERTEX_SHADER, vertexSource);
  const fragmentShader = compileShader(gl, gl.FRAGMENT_SHADER, fragmentSource);

  const program = gl.createProgram();
  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);
  assert.strictEqual(
      gl.getProgramParameter(program, gl.LINK_STATUS), true,
      gl.getProgramInfoLog(program));
  return program;
}

function createProgram(gl) {
  return createProgramFromSources(gl, `#version 300 es
precision highp float;
in vec2 a_position;
in vec2 a_offset;
void main() {
  gl_PointSize = 4.0;
  gl_Position = vec4(a_position + a_offset, 0.0, 1.0);
}
`, `#version 300 es
precision highp float;
out vec4 out_color;
void main() {
  out_color = vec4(1.0, 0.0, 0.0, 1.0);
}
`);
}

function testInfoLogEmptyStrings(gl) {
  const vertexShader = compileShader(gl, gl.VERTEX_SHADER, `
attribute vec2 a_position;
void main() {
  gl_PointSize = 1.0;
  gl_Position = vec4(a_position, 0.0, 1.0);
}
`);
  assert.strictEqual(gl.getShaderInfoLog(vertexShader), "");

  const fragmentShader = compileShader(gl, gl.FRAGMENT_SHADER, `
precision mediump float;
void main() {
  gl_FragColor = vec4(1.0);
}
`);
  assert.strictEqual(gl.getShaderInfoLog(fragmentShader), "");

  const program = gl.createProgram();
  assert.strictEqual(gl.getProgramInfoLog(program), "");
  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);
  assert.strictEqual(gl.getProgramParameter(program, gl.LINK_STATUS), true);
  assert.strictEqual(gl.getProgramInfoLog(program), "");
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

function assertNumberNear(actual, expected, label, tolerance = 1e-6) {
  assert.strictEqual(
      typeof actual, "number", `${label}: expected number, got ${actual}`);
  assert(
      Math.abs(actual - expected) <= tolerance,
      `${label}: expected ${expected}, got ${actual}`);
}

function assertArrayNear(actual, expected, label, tolerance = 1e-6) {
  assert(Array.isArray(actual), `${label}: expected array, got ${actual}`);
  assert.strictEqual(
      actual.length, expected.length,
      `${label}: expected length ${expected.length}, got ${actual.length}`);
  for (let i = 0; i < expected.length; ++i) {
    assertNumberNear(actual[i], expected[i], `${label}[${i}]`, tolerance);
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

function readTexture2DIntegerPixel(gl, texture, x, y) {
  const framebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
  gl.framebufferTexture2D(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
  const pixel = new Uint8Array(4);
  gl.readPixels(x, y, 1, 1, gl.RGBA_INTEGER, gl.UNSIGNED_BYTE, pixel);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.deleteFramebuffer(framebuffer);
  return pixel;
}

function readTextureLayerPixel(gl, texture, layer, x, y) {
  const previousReadFramebuffer =
      gl.getParameter(gl.READ_FRAMEBUFFER_BINDING);
  const previousDrawFramebuffer =
      gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING);
  const previousReadBuffer = gl.getParameter(gl.READ_BUFFER);
  const framebuffer = gl.createFramebuffer();
  const pixel = new Uint8Array(4);
  try {
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, framebuffer);
    gl.framebufferTextureLayer(
        gl.READ_FRAMEBUFFER, gl.COLOR_ATTACHMENT0, texture, 0, layer);
    assert.strictEqual(
        gl.checkFramebufferStatus(gl.READ_FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
    gl.readBuffer(gl.COLOR_ATTACHMENT0);
    gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  } finally {
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, previousReadFramebuffer);
    gl.readBuffer(previousReadBuffer);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, previousDrawFramebuffer);
    gl.deleteFramebuffer(framebuffer);
  }
  return pixel;
}

function configureTexture(gl, target) {
  gl.texParameteri(target, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(target, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(target, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(target, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
}

function configureLayeredTexture(gl, target) {
  configureTexture(gl, target);
  gl.texParameteri(target, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
}

function layeredTexturePixel(layer, x, y) {
  return [
    30 + layer * 60 + x * 20,
    40 + layer * 35 + y * 70,
    220 - layer * 45 - x * 25 - y * 15,
    255
  ];
}

function layeredTextureData(width, height, layers) {
  const data = new Uint8Array(width * height * layers * 4);
  for (let layer = 0; layer < layers; ++layer) {
    for (let y = 0; y < height; ++y) {
      for (let x = 0; x < width; ++x) {
        const offset = (((layer * height + y) * width + x) * 4);
        data.set(layeredTexturePixel(layer, x, y), offset);
      }
    }
  }
  return data;
}

function solidLayeredTextureData(width, height, layers, color) {
  const data = new Uint8Array(width * height * layers * 4);
  for (let offset = 0; offset < data.length; offset += 4) {
    data.set(color, offset);
  }
  return data;
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

function testSupportedExtensionsReflectGetExtension(gl) {
  const supported = gl.getSupportedExtensions();
  assert(Array.isArray(supported), "getSupportedExtensions should return an array");
  assert.strictEqual(
      new Set(supported).size, supported.length,
      "getSupportedExtensions should not contain duplicates");
  for (const name of supported) {
    assert(!name.startsWith("GL_"),
           `getSupportedExtensions should not expose raw GL extension ${name}`);
    assert.notStrictEqual(
        gl.getExtension(name), null,
        `${name} is listed but getExtension returned null`);
  }

  const knownExtensions = [
    "ANGLE_instanced_arrays",
    "EXT_blend_minmax",
    "EXT_color_buffer_float",
    "WEBGL_color_buffer_float",
    "EXT_color_buffer_half_float",
    "EXT_frag_depth",
    "EXT_sRGB",
    "EXT_shader_texture_lod",
    "EXT_texture_filter_anisotropic",
    "OES_element_index_uint",
    "OES_standard_derivatives",
    "OES_texture_float",
    "OES_texture_float_linear",
    "OES_texture_half_float",
    "OES_texture_half_float_linear",
    "OES_vertex_array_object",
    "WEBGL_debug_renderer_info",
    "WEBGL_depth_texture",
    "WEBGL_draw_buffers",
    "WEBGL_lose_context"
  ];
  for (const name of knownExtensions) {
    if (gl.getExtension(name) !== null) {
      assert(
          supported.includes(name),
          `${name} is returned by getExtension but missing from getSupportedExtensions`);
    }
  }
  assert(supported.includes("ANGLE_instanced_arrays"));
  assert(supported.includes("OES_vertex_array_object"));
  assert(supported.includes("WEBGL_draw_buffers"));
  assert.notStrictEqual(gl.getExtension("angle_instanced_arrays"), null);
  assert.notStrictEqual(gl.getExtension("oes_vertex_array_object"), null);
  assert.notStrictEqual(gl.getExtension("webgl_draw_buffers"), null);

  if (supported.includes("EXT_color_buffer_float") &&
      supported.includes("WEBGL_color_buffer_float")) {
    const extColorBufferFloat = gl.getExtension("EXT_color_buffer_float");
    const webglColorBufferFloat = gl.getExtension("WEBGL_color_buffer_float");
    assert.notStrictEqual(
        extColorBufferFloat, null,
        "EXT_color_buffer_float is listed but getExtension returned null");
    assert.notStrictEqual(
        webglColorBufferFloat, null,
        "WEBGL_color_buffer_float is listed but getExtension returned null");

    const colorBufferFloatConstants = [
      ["RGBA32F_EXT", "RGBA32F"],
      ["RGB32F_EXT", "RGB32F"],
      [
        "FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE_EXT",
        "FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE"
      ],
      ["UNSIGNED_NORMALIZED_EXT", "UNSIGNED_NORMALIZED"]
    ];
    for (const [extensionName, contextName] of colorBufferFloatConstants) {
      assert.strictEqual(
          extColorBufferFloat[extensionName],
          webglColorBufferFloat[extensionName],
          `${extensionName} should match across color buffer float aliases`);
      if (webglColorBufferFloat[extensionName] !== undefined &&
          gl[contextName] !== undefined) {
        assert.strictEqual(
            webglColorBufferFloat[extensionName], gl[contextName],
            `${extensionName} should match ${contextName}`);
      }
    }
  }

  assert.strictEqual(gl.getExtension("NOT_A_WEBGL_EXTENSION"), null);
  assertNoError(gl, "getExtension browser-compatible names");
}

function testUnsupportedExtensionDoesNotWriteStderr() {
  const result = spawnSync(process.execPath, ["-e", `
    const assert = require("assert");
    const nodeGles = require(".");
    const gl = nodeGles.createWebGLRenderingContext({
      width: 4,
      height: 4,
      majorVersion: 3,
      minorVersion: 0
    });
    assert.strictEqual(gl.getExtension("NOT_A_WEBGL_EXTENSION"), null);
  `], {
    cwd: path.resolve(__dirname, ".."),
    encoding: "utf8"
  });
  assert.strictEqual(
      result.status, 0,
      `unsupported extension child failed\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
  assert.strictEqual(
      result.stderr, "",
      `unsupported extension wrote stderr:\n${result.stderr}`);
}

function testGetParameterSemantics(gl) {
  const viewport = gl.getParameter(gl.VIEWPORT);
  assert.deepStrictEqual(viewport, [0, 0, 16, 16]);
  const scissor = gl.getParameter(gl.SCISSOR_BOX);
  assert.deepStrictEqual(scissor, [0, 0, 16, 16]);
  assert.strictEqual(gl.getError(), gl.NO_ERROR);

  assert.deepStrictEqual(gl.getParameter(gl.COLOR_CLEAR_VALUE), [0, 0, 0, 0]);
  assert.deepStrictEqual(gl.getParameter(gl.BLEND_COLOR), [0, 0, 0, 0]);
  assert.deepStrictEqual(gl.getParameter(gl.DEPTH_RANGE), [0, 1]);
  gl.clearColor(0.125, 0.25, 0.5, 0.875);
  assertArrayNear(
      gl.getParameter(gl.COLOR_CLEAR_VALUE), [0.125, 0.25, 0.5, 0.875],
      "mutated COLOR_CLEAR_VALUE");
  gl.blendColor(0.0625, 0.1875, 0.3125, 0.4375);
  assertArrayNear(
      gl.getParameter(gl.BLEND_COLOR), [0.0625, 0.1875, 0.3125, 0.4375],
      "mutated BLEND_COLOR");
  gl.depthRange(0.25, 0.75);
  assertArrayNear(
      gl.getParameter(gl.DEPTH_RANGE), [0.25, 0.75],
      "mutated DEPTH_RANGE");

  const lineWidthRange = gl.getParameter(gl.ALIASED_LINE_WIDTH_RANGE);
  assert(Array.isArray(lineWidthRange));
  const defaultLineWidth = gl.getParameter(gl.LINE_WIDTH);
  assert.strictEqual(typeof defaultLineWidth, "number");
  let testLineWidth = null;
  if (lineWidthRange[1] > defaultLineWidth) {
    testLineWidth = Math.min(lineWidthRange[1], defaultLineWidth + 1);
  } else if (lineWidthRange[0] < defaultLineWidth) {
    testLineWidth = lineWidthRange[0];
  }
  if (testLineWidth !== null &&
      Math.abs(testLineWidth - defaultLineWidth) > 1e-6) {
    gl.lineWidth(testLineWidth);
    assertNumberNear(
        gl.getParameter(gl.LINE_WIDTH), testLineWidth,
        "mutated LINE_WIDTH", 1e-5);
    gl.lineWidth(defaultLineWidth);
  }
  gl.clearColor(0, 0, 0, 0);
  gl.blendColor(0, 0, 0, 0);
  gl.depthRange(0, 1);
  assertNoError(gl, "mutated float getParameter state");

  assert.deepStrictEqual(gl.getParameter(gl.COLOR_WRITEMASK),
                         [true, true, true, true]);
  assert.strictEqual(typeof gl.getParameter(gl.DEPTH_WRITEMASK), "boolean");
  assert(Array.isArray(gl.getParameter(gl.COMPRESSED_TEXTURE_FORMATS)));

  assert.strictEqual(gl.getParameter(gl.ARRAY_BUFFER_BINDING), 0);
  assert.strictEqual(gl.getParameter(gl.CURRENT_PROGRAM), 0);
  assert.strictEqual(gl.getParameter(gl.VERTEX_ARRAY_BINDING), 0);
  assert.strictEqual(gl.getParameter(gl.SAMPLER_BINDING), 0);
  assert(Number.isInteger(gl.getParameter(gl.MAX_3D_TEXTURE_SIZE)));
  assert(Number.isInteger(gl.getParameter(gl.MAX_ARRAY_TEXTURE_LAYERS)));
  assert(Number.isInteger(gl.getParameter(gl.MAX_TEXTURE_SIZE)));
  const maxDrawBuffers = gl.getParameter(gl.MAX_DRAW_BUFFERS);
  const maxColorAttachments = gl.getParameter(gl.MAX_COLOR_ATTACHMENTS);
  assert(Number.isInteger(maxDrawBuffers));
  assert(Number.isInteger(maxColorAttachments));
  assert.strictEqual(gl.getParameter(gl.DRAW_BUFFER0), gl.BACK);
  if (maxDrawBuffers > 1 && maxColorAttachments > 1) {
    const drawBufferTextures = [gl.createTexture(), gl.createTexture()];
    for (const texture of drawBufferTextures) {
      gl.bindTexture(gl.TEXTURE_2D, texture);
      configureTexture(gl, gl.TEXTURE_2D);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA,
                    gl.UNSIGNED_BYTE, null);
    }

    const drawBufferFramebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, drawBufferFramebuffer);
    gl.framebufferTexture2D(
        gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D,
        drawBufferTextures[0], 0);
    gl.framebufferTexture2D(
        gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D,
        drawBufferTextures[1], 0);
    assert.strictEqual(
        gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
    assert.strictEqual(
        gl.getParameter(gl.DRAW_BUFFER0), gl.COLOR_ATTACHMENT0);
    assert.strictEqual(
        gl.getParameter(gl.DRAW_BUFFER1), gl.COLOR_ATTACHMENT1);

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    assert.strictEqual(gl.getParameter(gl.DRAW_BUFFER0), gl.BACK);
    gl.bindTexture(gl.TEXTURE_2D, null);
    gl.deleteFramebuffer(drawBufferFramebuffer);
    gl.deleteTexture(drawBufferTextures[0]);
    gl.deleteTexture(drawBufferTextures[1]);
    assertNoError(gl, "FBO draw buffer getParameter state");
  }
  if (maxDrawBuffers > 1) {
    assert(Number.isInteger(gl.getParameter(
        gl.DRAW_BUFFER0 + maxDrawBuffers - 1)));
  }
  if (maxDrawBuffers < 16) {
    assert.strictEqual(gl.getParameter(gl.DRAW_BUFFER0 + maxDrawBuffers), null);
    assert.strictEqual(gl.getError(), gl.INVALID_ENUM);
  }
  assert(gl.getParameter(gl.MAX_ELEMENT_INDEX) >= 0x7fffffff);
  assert.strictEqual(typeof gl.getParameter(gl.MAX_CLIENT_WAIT_TIMEOUT_WEBGL),
                     "number");

  const anisotropy = gl.getExtension("EXT_texture_filter_anisotropic");
  if (anisotropy) {
    assert.strictEqual(
        typeof gl.getParameter(anisotropy.MAX_TEXTURE_MAX_ANISOTROPY_EXT),
        "number");
  }
  const derivatives = gl.getExtension("OES_standard_derivatives");
  if (derivatives) {
    assert(Number.isInteger(
        gl.getParameter(derivatives.FRAGMENT_SHADER_DERIVATIVE_HINT_OES)));
  }
  assert.strictEqual(gl.getError(), gl.NO_ERROR);

  assert.strictEqual(gl.getParameter(0xdeadbeef), null);
  assert.strictEqual(gl.getError(), gl.INVALID_ENUM);
  assert.strictEqual(gl.getError(), gl.NO_ERROR);
}

function testBufferCopyAndReadback(gl) {
  const source = gl.createBuffer();
  const destination = gl.createBuffer();
  gl.bindBuffer(gl.COPY_READ_BUFFER, source);
  gl.bufferData(
      gl.COPY_READ_BUFFER, new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]),
      gl.STATIC_DRAW);
  gl.bindBuffer(gl.COPY_WRITE_BUFFER, destination);
  gl.bufferData(gl.COPY_WRITE_BUFFER, 8, gl.DYNAMIC_READ);
  gl.copyBufferSubData(gl.COPY_READ_BUFFER, gl.COPY_WRITE_BUFFER, 2, 1, 4);

  const full = new Uint8Array(8);
  gl.getBufferSubData(gl.COPY_WRITE_BUFFER, 0, full);
  assert.deepStrictEqual(Array.from(full), [0, 3, 4, 5, 6, 0, 0, 0]);

  const partial = new Uint8Array([99, 99, 99, 99, 99, 99]);
  gl.getBufferSubData(gl.COPY_WRITE_BUFFER, 1, partial, 1, 4);
  assert.deepStrictEqual(Array.from(partial), [99, 3, 4, 5, 6, 99]);

  gl.bindBuffer(gl.COPY_READ_BUFFER, null);
  gl.bindBuffer(gl.COPY_WRITE_BUFFER, null);
  gl.deleteBuffer(source);
  gl.deleteBuffer(destination);
  assertNoError(gl, "buffer copy and readback");
}

function testSamplerObjects(gl) {
  const sampler = gl.createSampler();
  assert.notStrictEqual(sampler, null);
  gl.bindSampler(0, sampler);
  assert.strictEqual(gl.isSampler(sampler), true);
  assert.strictEqual(gl.getParameter(gl.SAMPLER_BINDING), sampler);

  gl.samplerParameteri(sampler, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.samplerParameteri(sampler, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.samplerParameterf(sampler, gl.TEXTURE_MIN_LOD, -2.5);
  assert.strictEqual(
      gl.getSamplerParameter(sampler, gl.TEXTURE_MIN_FILTER), gl.NEAREST);
  assert.strictEqual(
      gl.getSamplerParameter(sampler, gl.TEXTURE_MAG_FILTER), gl.LINEAR);
  assert.strictEqual(
      gl.getSamplerParameter(sampler, gl.TEXTURE_MIN_LOD), -2.5);

  gl.bindSampler(0, null);
  gl.deleteSampler(sampler);
  assertNoError(gl, "sampler objects");
}

function testSyncObjects(gl) {
  const sync = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0);
  assert.notStrictEqual(sync, null);
  assert.strictEqual(gl.isSync(sync), true);
  assert.strictEqual(
      gl.getSyncParameter(sync, gl.SYNC_CONDITION),
      gl.SYNC_GPU_COMMANDS_COMPLETE);

  gl.flush();
  const waitResult = gl.clientWaitSync(sync, 0, 0);
  assert([
    gl.ALREADY_SIGNALED,
    gl.CONDITION_SATISFIED,
    gl.TIMEOUT_EXPIRED
  ].includes(waitResult), `unexpected clientWaitSync result ${waitResult}`);

  const syncStatus = gl.getSyncParameter(sync, gl.SYNC_STATUS);
  assert([gl.SIGNALED, gl.UNSIGNALED].includes(syncStatus),
         `unexpected sync status ${syncStatus}`);
  gl.waitSync(sync, 0, gl.TIMEOUT_IGNORED);
  gl.deleteSync(sync);
  assertNoError(gl, "sync objects");
}

function testTypedArrayTextureUploadOffsets(gl) {
  const src = new Uint8Array([
    1, 2, 3, 4,
    10, 20, 30, 255
  ]);

  const imageTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, imageTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, src, 4);
  assertPixelNear(
      readTexture2DPixel(gl, imageTexture, 0, 0), [10, 20, 30, 255],
      "texImage2D typed-array srcOffset");

  const subTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, subTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, null);
  gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 1, 1, gl.RGBA,
                   gl.UNSIGNED_BYTE, src, 4);
  assertPixelNear(
      readTexture2DPixel(gl, subTexture, 0, 0), [10, 20, 30, 255],
      "texSubImage2D typed-array srcOffset");

  const imageArrayTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D_ARRAY, imageArrayTexture);
  configureTexture(gl, gl.TEXTURE_2D_ARRAY);
  gl.texImage3D(gl.TEXTURE_2D_ARRAY, 0, gl.RGBA, 1, 1, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, src, 4);
  assertPixelNear(
      readTextureLayerPixel(gl, imageArrayTexture, 0, 0, 0), [10, 20, 30, 255],
      "texImage3D typed-array srcOffset");

  const subArrayTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D_ARRAY, subArrayTexture);
  configureTexture(gl, gl.TEXTURE_2D_ARRAY);
  gl.texImage3D(gl.TEXTURE_2D_ARRAY, 0, gl.RGBA, 1, 1, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, null);
  gl.texSubImage3D(gl.TEXTURE_2D_ARRAY, 0, 0, 0, 0, 1, 1, 1, gl.RGBA,
                   gl.UNSIGNED_BYTE, src, 4);
  assertPixelNear(
      readTextureLayerPixel(gl, subArrayTexture, 0, 0, 0), [10, 20, 30, 255],
      "texSubImage3D typed-array srcOffset");

  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
  gl.viewport(0, 0, 16, 16);
  gl.clearColor(0.2, 0.4, 0.6, 1.0);
  gl.clear(gl.COLOR_BUFFER_BIT);
  const pixels = new Uint8Array([9, 9, 9, 9, 9, 9, 9, 9]);
  gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixels, 4);
  assert.deepStrictEqual(Array.from(pixels.subarray(0, 4)), [9, 9, 9, 9]);
  assertPixelNear(
      pixels.subarray(4, 8), [51, 102, 153, 255],
      "readPixels typed-array dstOffset", 3);
  assertNoError(gl, "typed-array texture upload offsets");
}

function testPixelBufferObjectNumericOffsets(gl) {
  const pboSource = new Uint8Array([
    1, 2, 3, 4,
    20, 40, 60, 255
  ]);
  const unpackBuffer = gl.createBuffer();
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, unpackBuffer);
  gl.bufferData(gl.PIXEL_UNPACK_BUFFER, pboSource, gl.STATIC_DRAW);

  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, 4);
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
  assertPixelNear(
      readTexture2DPixel(gl, texture, 0, 0), [20, 40, 60, 255],
      "texImage2D PBO numeric offset");

  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.viewport(0, 0, 16, 16);
  gl.clearColor(1.0, 0.0, 0.0, 1.0);
  gl.clear(gl.COLOR_BUFFER_BIT);
  const packBuffer = gl.createBuffer();
  gl.bindBuffer(gl.PIXEL_PACK_BUFFER, packBuffer);
  gl.bufferData(gl.PIXEL_PACK_BUFFER, new Uint8Array(8), gl.STREAM_READ);
  const packBufferBinding = gl.getParameter(gl.PIXEL_PACK_BUFFER_BINDING);
  const readFramebufferBinding = gl.getParameter(gl.READ_FRAMEBUFFER_BINDING);
  const drawFramebufferBinding = gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING);
  assertNoError(gl, "readPixels invalid PBO overload setup");
  assert.throws(
      () => gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, 4, 0),
      /readPixels dstOffset requires ArrayBufferView data/);
  const afterInvalid = new Uint8Array(8);
  gl.getBufferSubData(gl.PIXEL_PACK_BUFFER, 0, afterInvalid);
  assert.deepStrictEqual(
      Array.from(afterInvalid), [0, 0, 0, 0, 0, 0, 0, 0]);
  assert.strictEqual(
      gl.getParameter(gl.PIXEL_PACK_BUFFER_BINDING), packBufferBinding);
  assert.strictEqual(
      gl.getParameter(gl.READ_FRAMEBUFFER_BINDING), readFramebufferBinding);
  assert.strictEqual(
      gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING), drawFramebufferBinding);
  assertNoError(gl, "readPixels invalid PBO overload side effects");

  gl.clearColor(0.0, 1.0, 0.0, 1.0);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, 4);
  const packed = new Uint8Array(8);
  gl.getBufferSubData(gl.PIXEL_PACK_BUFFER, 0, packed);
  assert.deepStrictEqual(Array.from(packed.subarray(0, 4)), [0, 0, 0, 0]);
  assertPixelNear(
      packed.subarray(4, 8), [0, 255, 0, 255],
      "readPixels PBO numeric offset", 3);

  gl.bindBuffer(gl.PIXEL_PACK_BUFFER, null);
  gl.deleteBuffer(unpackBuffer);
  gl.deleteBuffer(packBuffer);
  gl.deleteTexture(texture);
  assertNoError(gl, "pixel buffer object numeric offsets");
}

function testMultisampleFramebufferOperations(gl) {
  const maxSamples = gl.getParameter(gl.MAX_SAMPLES);
  assert(maxSamples > 0, "MAX_SAMPLES should be positive");
  const samples = Math.min(4, maxSamples);

  const multisampleRenderbuffer = gl.createRenderbuffer();
  gl.bindRenderbuffer(gl.RENDERBUFFER, multisampleRenderbuffer);
  gl.renderbufferStorageMultisample(
      gl.RENDERBUFFER, samples, gl.RGBA8, 4, 4);
  const multisampleFramebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, multisampleFramebuffer);
  gl.framebufferRenderbuffer(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER,
      multisampleRenderbuffer);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
  gl.viewport(0, 0, 4, 4);
  gl.clearColor(0, 0, 1, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);

  const resolveTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, resolveTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 4, 4, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, null);
  const resolveFramebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, resolveFramebuffer);
  gl.framebufferTexture2D(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, resolveTexture, 0);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);

  gl.bindFramebuffer(gl.READ_FRAMEBUFFER, multisampleFramebuffer);
  gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, resolveFramebuffer);
  gl.blitFramebuffer(
      0, 0, 4, 4, 0, 0, 4, 4, gl.COLOR_BUFFER_BIT, gl.NEAREST);
  gl.bindFramebuffer(gl.FRAMEBUFFER, resolveFramebuffer);
  const pixel = new Uint8Array(4);
  gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  assertPixelNear(pixel, [0, 0, 255, 255], "multisample blit");

  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.deleteFramebuffer(multisampleFramebuffer);
  gl.deleteFramebuffer(resolveFramebuffer);
  gl.deleteRenderbuffer(multisampleRenderbuffer);
  gl.deleteTexture(resolveTexture);
  assertNoError(gl, "multisample framebuffer operations");
}

function testUnsignedIntegerUniforms(gl) {
  const program = createProgramFromSources(gl, `#version 300 es
void main() {
  vec2 positions[3] = vec2[3](
      vec2(-1.0, -1.0),
      vec2(3.0, -1.0),
      vec2(-1.0, 3.0));
  gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
`, `#version 300 es
precision highp float;
uniform uint u_scalar;
uniform uvec4 u_vector;
out vec4 out_color;
void main() {
  bool ok = u_scalar == 7u && all(equal(u_vector, uvec4(1u, 2u, 3u, 4u)));
  out_color = ok ? vec4(0.0, 1.0, 0.0, 1.0) :
                   vec4(1.0, 0.0, 0.0, 1.0);
}
`);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.viewport(0, 0, 16, 16);
  gl.clearColor(1, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.useProgram(program);
  gl.uniform1ui(gl.getUniformLocation(program, "u_scalar"), 7);
  gl.uniform4uiv(
      gl.getUniformLocation(program, "u_vector"),
      new Uint32Array([1, 2, 3, 4]));
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  const pixel = new Uint8Array(4);
  gl.readPixels(8, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  assertPixelNear(pixel, [0, 255, 0, 255], "unsigned integer uniforms");
  assertNoError(gl, "unsigned integer uniforms");
}

function testNonSquareUniformMatrices(gl) {
  const program = createProgramFromSources(gl, `#version 300 es
void main() {
  vec2 positions[3] = vec2[3](
      vec2(-1.0, -1.0),
      vec2(3.0, -1.0),
      vec2(-1.0, 3.0));
  gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
`, `#version 300 es
precision highp float;
uniform mat2x3 u_m23;
uniform mat4x2 u_m42;
out vec4 out_color;
void main() {
  float value = u_m23[0][0] + u_m23[1][2] + u_m42[3][1];
  out_color = abs(value - 9.0) < 0.01 ? vec4(0.0, 1.0, 0.0, 1.0) :
                                        vec4(1.0, 0.0, 0.0, 1.0);
}
`);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.viewport(0, 0, 16, 16);
  gl.clearColor(1, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.useProgram(program);
  gl.uniformMatrix2x3fv(
      gl.getUniformLocation(program, "u_m23"), false,
      new Float32Array([1, 0, 0, 0, 0, 3]));
  gl.uniformMatrix4x2fv(
      gl.getUniformLocation(program, "u_m42"), false,
      new Float32Array([0, 0, 0, 0, 0, 0, 0, 5]));
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  const pixel = new Uint8Array(4);
  gl.readPixels(8, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  assertPixelNear(pixel, [0, 255, 0, 255], "non-square uniform matrices");
  assertNoError(gl, "non-square uniform matrices");
}

function testDestroyApi() {
  const gl = createContext();
  assert.strictEqual(typeof gl.destroy, "function");
  assert.strictEqual(typeof gl.dispose, "function");
  assert.strictEqual(gl.isContextLost(), false);
  gl.clearColor(0, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  assertNoError(gl, "context before destroy");
  assert.doesNotThrow(() => gl.destroy());
  assert.strictEqual(gl.isContextLost(), true);
  assert.doesNotThrow(() => gl.dispose());
  assert.throws(() => gl.clear(gl.COLOR_BUFFER_BIT), /destroyed/);
}

function testLoseContextApi() {
  const gl = createContext();
  const ext = gl.getExtension("WEBGL_lose_context");
  assert.notStrictEqual(ext, null, "WEBGL_lose_context should be supported");
  assert.strictEqual(typeof ext.loseContext, "function");
  assert.strictEqual(gl.isContextLost(), false);
  gl.clearColor(0, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  assertNoError(gl, "context before loseContext");
  Object.defineProperty(gl, "destroy", {
    configurable: true,
    value: () => {
      throw new Error("loseContext must not call mutable gl.destroy");
    }
  });
  assert.doesNotThrow(() => ext.loseContext());
  assert.strictEqual(gl.isContextLost(), true, "isContextLost should be true after loseContext");
  assert.throws(() => gl.clear(gl.COLOR_BUFFER_BIT), /destroyed/,
                "GL call after loseContext should throw");
  // Second call should be a no-op.
  assert.doesNotThrow(() => ext.loseContext());
}

function testSequentialContextCleanup() {
  const result = spawnSync(process.execPath, ["--expose-gc", "-e", `
    const assert = require("assert");
    const nodeGles = require(".");
    for (let i = 0; i < 16; ++i) {
      let gl = nodeGles.createWebGLRenderingContext({
        width: 64,
        height: 64,
        majorVersion: 3,
        minorVersion: 0
      });
      gl.clearColor(i % 2, 0, 0, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
      const pixel = new Uint8Array(4);
      gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
      assert.strictEqual(gl.getError(), gl.NO_ERROR);
      gl = null;
      if (global.gc) global.gc();
    }
    if (global.gc) {
      for (let i = 0; i < 4; ++i) global.gc();
    }
  `], {
    cwd: path.resolve(__dirname, ".."),
    encoding: "utf8"
  });
  assert.strictEqual(
      result.status, 0,
      `sequential context cleanup failed\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
  assert.strictEqual(
      result.stderr, "",
      `sequential context cleanup wrote stderr:\n${result.stderr}`);
}

function testLoseContextSequentialCleanup() {
  // Simulates the wasm-gerber-renderer pattern: create → render → loseContext,
  // repeated in the same process. Exercises the repeated lifecycle path.
  const result = spawnSync(process.execPath, ["-e", `
    const assert = require("assert");
    const nodeGles = require(".");
    for (let i = 0; i < 16; ++i) {
      const gl = nodeGles.createWebGLRenderingContext({
        width: 64,
        height: 64,
        majorVersion: 3,
        minorVersion: 0
      });
      const ext = gl.getExtension("WEBGL_lose_context");
      gl.clearColor(i % 2, 0, 0, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
      const pixel = new Uint8Array(4);
      gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
      const expected = i % 2 === 0 ? 0 : 255;
      assert.strictEqual(pixel[0], expected,
          "pixel[0] mismatch on iteration " + i);
      ext.loseContext();
      assert.strictEqual(gl.isContextLost(), true,
          "isContextLost should be true after loseContext on iteration " + i);
    }
  `], {
    cwd: path.resolve(__dirname, ".."),
    encoding: "utf8"
  });
  assert.strictEqual(
      result.status, 0,
      `loseContext lifecycle failed\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
  assert.strictEqual(
      result.stderr, "",
      `loseContext lifecycle wrote stderr:\n${result.stderr}`);
}

function testFailedContextCreationDoesNotPoisonProcess() {
  const result = spawnSync(process.execPath, ["-e", `
    const assert = require("assert");
    const nodeGles = require(".");
    let threw = false;
    try {
      nodeGles.createWebGLRenderingContext({
        width: 16,
        height: 16,
        majorVersion: 99,
        minorVersion: 0
      });
    } catch (error) {
      threw = true;
    }
    assert.strictEqual(threw, true, "unsupported context creation should fail");
    const gl = nodeGles.createWebGLRenderingContext({
      width: 16,
      height: 16,
      majorVersion: 3,
      minorVersion: 0
    });
    gl.clearColor(0, 0, 1, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    const pixel = new Uint8Array(4);
    gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
    assert.strictEqual(gl.getError(), gl.NO_ERROR);
    assert.deepStrictEqual(Array.from(pixel), [0, 0, 255, 255]);
    gl.destroy();
  `], {
    cwd: path.resolve(__dirname, ".."),
    encoding: "utf8"
  });
  assert.strictEqual(
      result.status, 0,
      `failed context recovery failed\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
}

function testDeleteNullNoOps(gl) {
  const deleteMethods = [
    "deleteBuffer",
    "deleteFramebuffer",
    "deleteProgram",
    "deleteRenderbuffer",
    "deleteShader",
    "deleteTexture",
    "deleteQuery",
    "deleteSampler",
    "deleteSync",
    "deleteTransformFeedback",
    "deleteVertexArray"
  ];
  for (const method of deleteMethods) {
    assert.strictEqual(typeof gl[method], "function", `${method} missing`);
    assert.doesNotThrow(() => gl[method](null), `${method}(null) threw`);
  }
  assertNoError(gl, "null delete no-ops");
}

function testExtraArgumentsAreIgnored(gl) {
  gl.clearColor(0, 0, 0, 1, "ignored");
  gl.viewport(0, 0, 16, 16, "ignored");
  gl.scissor(0, 0, 16, 16, "ignored");

  const shader = gl.createShader(gl.VERTEX_SHADER);
  gl.shaderSource(shader, `
attribute vec2 a_position;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
}
`, "ignored");
  gl.compileShader(shader, "ignored");
  assert.strictEqual(gl.getShaderParameter(shader, gl.COMPILE_STATUS), true);
  gl.deleteShader(shader, 0, []);

  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture, "ignored");
  gl.texImage2D(
      gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
      new Uint8Array([255, 0, 0, 255]), 0, "ignored");
  gl.texImage2D(
      gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE,
      {width: 1, height: 1, data: new Uint8Array([0, 255, 0, 255])},
      "ignored", "ignored", "ignored");
  gl.deleteTexture(texture, "ignored");

  assertNoError(gl, "extra arguments ignored");
  assert.throws(() => gl.clearColor(0, 0, 0), /Incorrect number of arguments/);
}

function testDeleteSyncUsesOwningContext() {
  const gl1 = createContext({width: 4, height: 4});
  const sync = gl1.fenceSync(gl1.SYNC_GPU_COMMANDS_COMPLETE, 0);
  gl1.flush();

  const gl2 = createContext({width: 16, height: 16});
  gl2.clearColor(0, 1, 0, 1);
  gl2.clear(gl2.COLOR_BUFFER_BIT);
  assertNoError(gl2, "gl2 before cross-context deleteSync");

  assert.doesNotThrow(() => gl2.deleteSync(sync));

  const pixel = new Uint8Array(4);
  gl2.readPixels(15, 15, 1, 1, gl2.RGBA, gl2.UNSIGNED_BYTE, pixel);
  assertNoError(gl2, "deleteSync restores caller context");
  assert.deepStrictEqual(Array.from(pixel), [0, 255, 0, 255]);
  assertNoError(gl1, "deleteSync owner context");

  gl2.destroy();
  gl1.destroy();
}

function testSyncFinalizerRestoresCurrentContext() {
  const result = spawnSync(process.execPath, ["--expose-gc", "-e", `
    const assert = require("assert");
    const nodeGles = require(".");

    const gl1 = nodeGles.createWebGLRenderingContext({
      width: 4,
      height: 4,
      majorVersion: 3,
      minorVersion: 0
    });
    let syncs = [];
    for (let i = 0; i < 32; ++i) {
      syncs.push(gl1.fenceSync(gl1.SYNC_GPU_COMMANDS_COMPLETE, 0));
    }
    gl1.flush();

    const gl2 = nodeGles.createWebGLRenderingContext({
      width: 16,
      height: 16,
      majorVersion: 3,
      minorVersion: 0
    });
    gl2.clearColor(0, 1, 0, 1);
    gl2.clear(gl2.COLOR_BUFFER_BIT);
    assert.strictEqual(gl2.getError(), gl2.NO_ERROR);

    syncs = null;
    for (let i = 0; i < 8; ++i) {
      global.gc();
    }

    const pixel = new Uint8Array(4);
    gl2.readPixels(15, 15, 1, 1, gl2.RGBA, gl2.UNSIGNED_BYTE, pixel);
    assert.strictEqual(gl2.getError(), gl2.NO_ERROR);
    assert.deepStrictEqual(Array.from(pixel), [0, 255, 0, 255]);

    gl2.destroy();
    gl1.destroy();
  `], {
    cwd: path.resolve(__dirname, ".."),
    encoding: "utf8"
  });
  assert.strictEqual(
      result.status, 0,
      `sync finalizer current restore failed\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`);
  assert.strictEqual(
      result.stderr, "",
      `sync finalizer current restore wrote stderr:\n${result.stderr}`);
}

function testMultipleLiveContextsUseOwnCurrentContext() {
  const gl1 = createContext({ width: 16, height: 16 });
  gl1.clearColor(1, 0, 0, 1);
  gl1.clear(gl1.COLOR_BUFFER_BIT);
  assertNoError(gl1, "gl1 initial clear");

  const gl2 = createContext({ width: 16, height: 16 });
  gl2.clearColor(0, 1, 0, 1);
  gl2.clear(gl2.COLOR_BUFFER_BIT);
  assertNoError(gl2, "gl2 initial clear");

  const pixel1 = new Uint8Array(4);
  gl1.readPixels(0, 0, 1, 1, gl1.RGBA, gl1.UNSIGNED_BYTE, pixel1);
  assertNoError(gl1, "gl1 read after gl2 became current");
  assert.deepStrictEqual(Array.from(pixel1), [255, 0, 0, 255]);

  const pixel2 = new Uint8Array(4);
  gl2.readPixels(0, 0, 1, 1, gl2.RGBA, gl2.UNSIGNED_BYTE, pixel2);
  assertNoError(gl2, "gl2 read after gl1 call");
  assert.deepStrictEqual(Array.from(pixel2), [0, 255, 0, 255]);

  gl1.destroy();
  gl2.destroy();
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

  const ignore3DStateTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, ignore3DStateTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.pixelStorei(gl.UNPACK_IMAGE_HEIGHT, 1);
  gl.pixelStorei(gl.UNPACK_SKIP_IMAGES, 2);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, pixels2D);
  assertPixelNear(
      readTexture2DPixel(gl, ignore3DStateTexture, 0, 0),
      [0, 0, 255, 255],
      "texImage2D ignores 3D-only unpack state");
  gl.pixelStorei(gl.UNPACK_IMAGE_HEIGHT, 0);
  gl.pixelStorei(gl.UNPACK_SKIP_IMAGES, 0);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);

  const undersizedSourceTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, undersizedSourceTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, new Uint8Array([1, 2, 3, 4]), 1);
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, null);
  assertNoError(gl, "texImage2D null allocation after undersized srcData");
  gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 1, 1, gl.RGBA,
                   gl.UNSIGNED_BYTE, new Uint8Array([1, 2, 3, 4]), 1);
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  assert.doesNotThrow(() => {
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 2, 0, 0xdead,
                  gl.UNSIGNED_BYTE, new Uint8Array(8));
  });
  assert.strictEqual(gl.getError(), gl.INVALID_ENUM);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);

  const pboTexture2D = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, pboTexture2D);
  configureTexture(gl, gl.TEXTURE_2D);
  const pbo2D = gl.createBuffer();
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, pbo2D);
  gl.bufferData(gl.PIXEL_UNPACK_BUFFER, pixels2D, gl.STATIC_DRAW);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, pixels2D);
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, {
    width: 2,
    height: 2,
    data: pixels2D
  });
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, 0);
  assertPixelNear(
      readTexture2DPixel(gl, pboTexture2D, 0, 0), [0, 0, 255, 255],
      "texImage2D PBO UNPACK_FLIP_Y_WEBGL");

  const pbo2DIgnore3DStateTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, pbo2DIgnore3DStateTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.pixelStorei(gl.UNPACK_IMAGE_HEIGHT, 1);
  gl.pixelStorei(gl.UNPACK_SKIP_IMAGES, 2);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, 0);
  assertPixelNear(
      readTexture2DPixel(gl, pbo2DIgnore3DStateTexture, 0, 0),
      [0, 0, 255, 255],
      "texImage2D PBO ignores 3D-only unpack state");
  gl.pixelStorei(gl.UNPACK_IMAGE_HEIGHT, 0);
  gl.pixelStorei(gl.UNPACK_SKIP_IMAGES, 0);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 1);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, pixels2D);
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 3);
  gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 2);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, pixels2D);
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
  gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 0);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, new Uint8Array([255, 0, 0, 255]));
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);

  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, pbo2D);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 3);
  gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 2);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, 0);
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
  gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 0);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);

  const pboMisaligned = gl.createBuffer();
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, pboMisaligned);
  gl.bufferData(gl.PIXEL_UNPACK_BUFFER, 33, gl.STATIC_DRAW);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,
                gl.UNSIGNED_SHORT, 1);
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
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

  const integerPremultiplyTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, integerPremultiplyTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, 1);
  gl.texImage2D(
      gl.TEXTURE_2D, 0, gl.RGBA8UI, 1, 1, 0, gl.RGBA_INTEGER,
      gl.UNSIGNED_BYTE, new Uint8Array([100, 50, 25, 128]));
  assert.deepStrictEqual(
      Array.from(readTexture2DIntegerPixel(gl, integerPremultiplyTexture, 0, 0)),
      [50, 25, 13, 128]);
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

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.pixelStorei(gl.UNPACK_IMAGE_HEIGHT, 1);
  gl.texImage3D(gl.TEXTURE_3D, 0, gl.RGBA, 2, 2, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, pixels2D);
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
  gl.pixelStorei(gl.UNPACK_IMAGE_HEIGHT, 0);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);

  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
  gl.pixelStorei(gl.UNPACK_IMAGE_HEIGHT, 3);
  gl.pixelStorei(gl.UNPACK_SKIP_ROWS, 2);
  gl.texImage3D(gl.TEXTURE_3D, 0, gl.RGBA, 2, 2, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, pixels2D);
  assert.strictEqual(gl.getError(), gl.INVALID_OPERATION);
  gl.pixelStorei(gl.UNPACK_SKIP_ROWS, 0);
  gl.pixelStorei(gl.UNPACK_IMAGE_HEIGHT, 0);
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

function testLayeredTextureOperations(gl) {
  const width = 2;
  const height = 2;
  const layers = 3;
  assert(
      gl.getParameter(gl.MAX_3D_TEXTURE_SIZE) >= layers,
      "MAX_3D_TEXTURE_SIZE should support the smoke texture");
  assert(
      gl.getParameter(gl.MAX_ARRAY_TEXTURE_LAYERS) >= layers,
      "MAX_ARRAY_TEXTURE_LAYERS should support the smoke texture");

  const uploadData = layeredTextureData(width, height, layers);
  const texture3D = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_3D, texture3D);
  configureLayeredTexture(gl, gl.TEXTURE_3D);
  gl.texImage3D(
      gl.TEXTURE_3D, 0, gl.RGBA8, width, height, layers, 0, gl.RGBA,
      gl.UNSIGNED_BYTE, uploadData);
  for (let layer = 0; layer < layers; ++layer) {
    for (let y = 0; y < height; ++y) {
      for (let x = 0; x < width; ++x) {
        assertPixelNear(
            readTextureLayerPixel(gl, texture3D, layer, x, y),
            layeredTexturePixel(layer, x, y),
            `texImage3D layer ${layer} pixel ${x},${y}`);
      }
    }
  }

  const textureArray = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D_ARRAY, textureArray);
  configureLayeredTexture(gl, gl.TEXTURE_2D_ARRAY);
  gl.texStorage3D(
      gl.TEXTURE_2D_ARRAY, 1, gl.RGBA8, width, height, layers);
  gl.texSubImage3D(
      gl.TEXTURE_2D_ARRAY, 0, 0, 0, 0, width, height, layers, gl.RGBA,
      gl.UNSIGNED_BYTE, uploadData);
  for (let layer = 0; layer < layers; ++layer) {
    for (let y = 0; y < height; ++y) {
      for (let x = 0; x < width; ++x) {
        assertPixelNear(
            readTextureLayerPixel(gl, textureArray, layer, x, y),
            layeredTexturePixel(layer, x, y),
            `texSubImage3D array layer ${layer} pixel ${x},${y}`);
      }
    }
  }

  const partial3D = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_3D, partial3D);
  configureLayeredTexture(gl, gl.TEXTURE_3D);
  gl.texStorage3D(gl.TEXTURE_3D, 1, gl.RGBA8, width, height, layers);
  gl.texSubImage3D(
      gl.TEXTURE_3D, 0, 1, 1, 2, 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE,
      new Uint8Array([9, 99, 199, 255]));
  assertPixelNear(
      readTextureLayerPixel(gl, partial3D, 2, 1, 1), [9, 99, 199, 255],
      "texSubImage3D partial 3D upload");

  const sourcePixels = new Uint8Array([
    11, 22, 33, 255, 44, 55, 66, 255,
    77, 88, 99, 255, 111, 122, 133, 255
  ]);
  const sourceTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, sourceTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(
      gl.TEXTURE_2D, 0, gl.RGBA8, width, height, 0, gl.RGBA,
      gl.UNSIGNED_BYTE, sourcePixels);
  const sourceFramebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, sourceFramebuffer);
  gl.framebufferTexture2D(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, sourceTexture, 0);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
  gl.readBuffer(gl.COLOR_ATTACHMENT0);

  const copiedArray = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D_ARRAY, copiedArray);
  configureLayeredTexture(gl, gl.TEXTURE_2D_ARRAY);
  gl.texStorage3D(
      gl.TEXTURE_2D_ARRAY, 1, gl.RGBA8, width, height, layers);
  gl.texSubImage3D(
      gl.TEXTURE_2D_ARRAY, 0, 0, 0, 0, width, height, layers, gl.RGBA,
      gl.UNSIGNED_BYTE,
      solidLayeredTextureData(width, height, layers, [1, 2, 3, 255]));
  gl.copyTexSubImage3D(gl.TEXTURE_2D_ARRAY, 0, 0, 0, 1, 0, 0, width, height);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.deleteFramebuffer(sourceFramebuffer);
  for (let y = 0; y < height; ++y) {
    for (let x = 0; x < width; ++x) {
      const sourceOffset = ((y * width + x) * 4);
      assertPixelNear(
          readTextureLayerPixel(gl, copiedArray, 1, x, y),
          Array.from(sourcePixels.subarray(sourceOffset, sourceOffset + 4)),
          `copyTexSubImage3D array layer pixel ${x},${y}`);
    }
  }
  assertPixelNear(
      readTextureLayerPixel(gl, copiedArray, 0, 1, 1), [1, 2, 3, 255],
      "copyTexSubImage3D keeps untouched array layer");

  const rendered3D = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_3D, rendered3D);
  configureLayeredTexture(gl, gl.TEXTURE_3D);
  gl.texStorage3D(gl.TEXTURE_3D, 1, gl.RGBA8, width, height, layers);
  gl.texSubImage3D(
      gl.TEXTURE_3D, 0, 0, 0, 0, width, height, layers, gl.RGBA,
      gl.UNSIGNED_BYTE,
      solidLayeredTextureData(width, height, layers, [4, 5, 6, 255]));
  const layerFramebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, layerFramebuffer);
  gl.framebufferTextureLayer(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, rendered3D, 0, 2);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
  gl.viewport(0, 0, width, height);
  gl.clearColor(0.8, 0.2, 0.4, 1.0);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.deleteFramebuffer(layerFramebuffer);
  assertPixelNear(
      readTextureLayerPixel(gl, rendered3D, 2, 1, 1), [204, 51, 102, 255],
      "framebufferTextureLayer 3D render target");
  assertPixelNear(
      readTextureLayerPixel(gl, rendered3D, 0, 1, 1), [4, 5, 6, 255],
      "framebufferTextureLayer keeps untouched 3D layer");

  gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
  assertNoError(gl, "layered texture operations");
}

function testFragmentShaderGpgpuMatrixMultiply(gl) {
  const matrixA = [
    [1, 2, 0, 1],
    [0, 1, 3, 2],
    [2, 0, 1, 1],
    [1, 1, 1, 0]
  ];
  const matrixB = [
    [1, 0, 2, 1],
    [3, 1, 0, 2],
    [0, 2, 1, 1],
    [1, 1, 1, 0]
  ];

  function expectedMatrix() {
    const expected = [];
    for (let row = 0; row < 4; ++row) {
      expected[row] = [];
      for (let col = 0; col < 4; ++col) {
        expected[row][col] = 0;
        for (let k = 0; k < 4; ++k) {
          expected[row][col] += matrixA[row][k] * matrixB[k][col];
        }
      }
    }
    return expected;
  }

  function matrixToTextureData(matrix) {
    const data = new Uint8Array(4 * 4 * 4);
    for (let row = 0; row < 4; ++row) {
      for (let col = 0; col < 4; ++col) {
        const offset = (row * 4 + col) * 4;
        data[offset] = matrix[row][col];
        data[offset + 3] = 255;
      }
    }
    return data;
  }

  function createMatrixTexture(matrix, usePixelUnpackBuffer) {
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    configureTexture(gl, gl.TEXTURE_2D);
    const data = matrixToTextureData(matrix);
    if (!usePixelUnpackBuffer) {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 4, 4, 0, gl.RGBA,
                    gl.UNSIGNED_BYTE, data);
      return {texture, pixelUnpackBuffer: null};
    }

    const pixelUnpackBuffer = gl.createBuffer();
    gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, pixelUnpackBuffer);
    gl.bufferData(gl.PIXEL_UNPACK_BUFFER, data, gl.STATIC_DRAW);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 4, 4, 0, gl.RGBA,
                  gl.UNSIGNED_BYTE, 0);
    gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
    return {texture, pixelUnpackBuffer};
  }

  function createOutputTarget(label) {
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    configureTexture(gl, gl.TEXTURE_2D);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 16, 16, 0, gl.RGBA,
                  gl.UNSIGNED_BYTE, null);

    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    gl.framebufferTexture2D(
        gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
    assert.strictEqual(
        gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE,
        `${label}: framebuffer incomplete`);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0]);
    gl.viewport(0, 0, 16, 16);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    return {texture, framebuffer, label};
  }

  function bindMatrixTextures(program, textureA, textureB) {
    gl.useProgram(program);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, textureA);
    gl.uniform1i(gl.getUniformLocation(program, "u_a"), 0);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, textureB);
    gl.uniform1i(gl.getUniformLocation(program, "u_b"), 1);
  }

  function verifyMatrixFramebuffer(framebuffer, label) {
    const expected = expectedMatrix();
    const pixel = new Uint8Array(4);
    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    gl.readBuffer(gl.COLOR_ATTACHMENT0);
    for (let row = 0; row < 4; ++row) {
      for (let col = 0; col < 4; ++col) {
        gl.readPixels(col, row, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
        assertPixelNear(
            pixel, [expected[row][col] * 16, row * 48, col * 48, 255],
            `${label} [${row}, ${col}]`, 1);
      }
    }
  }

  function disposeOutputTarget(target) {
    gl.deleteFramebuffer(target.framebuffer);
    gl.deleteTexture(target.texture);
  }

  const fullScreenProgram = createProgramFromSources(gl, `#version 300 es
precision highp float;
in vec2 a_position;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
}
`, `#version 300 es
precision highp float;
precision highp int;
uniform sampler2D u_a;
uniform sampler2D u_b;
out vec4 out_color;
void main() {
  ivec2 cell = ivec2(gl_FragCoord.xy);
  if (cell.x >= 4 || cell.y >= 4) {
    out_color = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  float sum = 0.0;
  for (int k = 0; k < 4; ++k) {
    float a = texelFetch(u_a, ivec2(k, cell.y), 0).r * 255.0;
    float b = texelFetch(u_b, ivec2(cell.x, k), 0).r * 255.0;
    sum += a * b;
  }

  out_color = vec4(sum * 16.0 / 255.0,
                   float(cell.y * 48) / 255.0,
                   float(cell.x * 48) / 255.0,
                   1.0);
}
`);

  const instancedProgram = createProgramFromSources(gl, `#version 300 es
precision highp float;
precision highp int;
in vec2 a_cell;
flat out ivec2 v_cell;
void main() {
  v_cell = ivec2(a_cell);
  vec2 pixel = a_cell + vec2(0.5, 0.5);
  vec2 ndc = pixel / vec2(16.0, 16.0) * 2.0 - 1.0;
  gl_Position = vec4(ndc, 0.0, 1.0);
  gl_PointSize = 1.0;
}
`, `#version 300 es
precision highp float;
precision highp int;
uniform sampler2D u_a;
uniform sampler2D u_b;
flat in ivec2 v_cell;
out vec4 out_color;
void main() {
  ivec2 cell = v_cell;
  float sum = 0.0;
  for (int k = 0; k < 4; ++k) {
    float a = texelFetch(u_a, ivec2(k, cell.y), 0).r * 255.0;
    float b = texelFetch(u_b, ivec2(cell.x, k), 0).r * 255.0;
    sum += a * b;
  }

  out_color = vec4(sum * 16.0 / 255.0,
                   float(cell.y * 48) / 255.0,
                   float(cell.x * 48) / 255.0,
                   1.0);
}
`);

  const sourceA = createMatrixTexture(matrixA, false);
  const sourceB = createMatrixTexture(matrixB, true);

  const arraysTarget = createOutputTarget("GPGPU drawArrays");
  const arraysVao = gl.createVertexArray();
  const arraysPositionBuffer = gl.createBuffer();
  gl.bindVertexArray(arraysVao);
  gl.bindBuffer(gl.ARRAY_BUFFER, arraysPositionBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]),
                gl.STATIC_DRAW);
  let positionLocation = gl.getAttribLocation(fullScreenProgram, "a_position");
  assert(positionLocation >= 0, "GPGPU position attribute not found");
  gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);
  gl.enableVertexAttribArray(positionLocation);
  bindMatrixTextures(fullScreenProgram, sourceA.texture, sourceB.texture);
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  verifyMatrixFramebuffer(arraysTarget.framebuffer, "GPGPU drawArrays");

  const elementsTarget = createOutputTarget("GPGPU drawElements");
  const elementsVao = gl.createVertexArray();
  const elementsPositionBuffer = gl.createBuffer();
  const elementBuffer = gl.createBuffer();
  gl.bindVertexArray(elementsVao);
  gl.bindBuffer(gl.ARRAY_BUFFER, elementsPositionBuffer);
  gl.bufferData(
      gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
      gl.STATIC_DRAW);
  positionLocation = gl.getAttribLocation(fullScreenProgram, "a_position");
  gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);
  gl.enableVertexAttribArray(positionLocation);
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, elementBuffer);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0, 1, 2, 2, 1, 3]),
                gl.STATIC_DRAW);
  bindMatrixTextures(fullScreenProgram, sourceA.texture, sourceB.texture);
  gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
  verifyMatrixFramebuffer(elementsTarget.framebuffer, "GPGPU drawElements");

  const blitTarget = createOutputTarget("GPGPU blitFramebuffer");
  gl.bindFramebuffer(gl.READ_FRAMEBUFFER, elementsTarget.framebuffer);
  gl.readBuffer(gl.COLOR_ATTACHMENT0);
  gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, blitTarget.framebuffer);
  gl.blitFramebuffer(
      0, 0, 4, 4, 0, 0, 4, 4, gl.COLOR_BUFFER_BIT, gl.NEAREST);
  verifyMatrixFramebuffer(blitTarget.framebuffer, "GPGPU blitFramebuffer");

  const instancedTarget = createOutputTarget("GPGPU instanced");
  const instancedVao = gl.createVertexArray();
  const cellBuffer = gl.createBuffer();
  const cells = [];
  for (let row = 0; row < 4; ++row) {
    for (let col = 0; col < 4; ++col) {
      cells.push(col, row);
    }
  }
  gl.bindVertexArray(instancedVao);
  gl.bindBuffer(gl.ARRAY_BUFFER, cellBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(cells), gl.STATIC_DRAW);
  const cellLocation = gl.getAttribLocation(instancedProgram, "a_cell");
  assert(cellLocation >= 0, "GPGPU instance cell attribute not found");
  gl.vertexAttribPointer(cellLocation, 2, gl.FLOAT, false, 0, 0);
  gl.enableVertexAttribArray(cellLocation);
  gl.vertexAttribDivisor(cellLocation, 1);
  bindMatrixTextures(instancedProgram, sourceA.texture, sourceB.texture);
  gl.drawArraysInstanced(gl.POINTS, 0, 1, 16);
  gl.vertexAttribDivisor(cellLocation, 0);
  verifyMatrixFramebuffer(instancedTarget.framebuffer, "GPGPU instanced");

  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.bindVertexArray(null);
  gl.bindBuffer(gl.ARRAY_BUFFER, null);
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
  gl.activeTexture(gl.TEXTURE1);
  gl.bindTexture(gl.TEXTURE_2D, null);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, null);
  gl.deleteBuffer(cellBuffer);
  gl.deleteVertexArray(instancedVao);
  disposeOutputTarget(instancedTarget);
  disposeOutputTarget(blitTarget);
  gl.deleteBuffer(elementBuffer);
  gl.deleteBuffer(elementsPositionBuffer);
  gl.deleteVertexArray(elementsVao);
  disposeOutputTarget(elementsTarget);
  gl.deleteBuffer(arraysPositionBuffer);
  gl.deleteVertexArray(arraysVao);
  disposeOutputTarget(arraysTarget);
  if (sourceB.pixelUnpackBuffer) {
    gl.deleteBuffer(sourceB.pixelUnpackBuffer);
  }
  gl.deleteTexture(sourceB.texture);
  gl.deleteTexture(sourceA.texture);
  gl.deleteProgram(instancedProgram);
  gl.deleteProgram(fullScreenProgram);
  assertNoError(gl, "fragment shader GPGPU matrix multiply variants");
}

function testFragmentShaderGpgpuFloatMatrixMultiply(gl) {
  const matrixA = [
    [0.5, 1.25, 0.0, 0.75],
    [1.0, 0.5, 0.25, 0.0],
    [0.25, 1.5, 0.5, 0.75],
    [1.25, 0.0, 0.75, 0.5]
  ];
  const matrixB = [
    [1.0, 0.5, 0.25, 1.5],
    [0.75, 1.25, 0.5, 0.0],
    [1.5, 0.25, 1.0, 0.5],
    [0.0, 1.0, 0.75, 0.25]
  ];

  function matrixToFloatTextureData(matrix) {
    const data = new Float32Array(4 * 4 * 4);
    for (let row = 0; row < 4; ++row) {
      for (let col = 0; col < 4; ++col) {
        const offset = (row * 4 + col) * 4;
        data[offset] = matrix[row][col];
        data[offset + 3] = 1;
      }
    }
    return data;
  }

  function createFloatMatrixTexture(matrix) {
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    configureTexture(gl, gl.TEXTURE_2D);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, 4, 4, 0, gl.RGBA, gl.FLOAT,
                  matrixToFloatTextureData(matrix));
    return texture;
  }

  const program = createProgramFromSources(gl, `#version 300 es
precision highp float;
in vec2 a_position;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
}
`, `#version 300 es
precision highp float;
precision highp int;
uniform sampler2D u_a;
uniform sampler2D u_b;
out vec4 out_color;
void main() {
  ivec2 cell = ivec2(gl_FragCoord.xy);
  if (cell.x >= 4 || cell.y >= 4) {
    out_color = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  float sum = 0.0;
  for (int k = 0; k < 4; ++k) {
    float a = texelFetch(u_a, ivec2(k, cell.y), 0).r;
    float b = texelFetch(u_b, ivec2(cell.x, k), 0).r;
    sum += a * b;
  }

  out_color = vec4(sum / 4.0,
                   float(cell.y * 48) / 255.0,
                   float(cell.x * 48) / 255.0,
                   1.0);
}
`);
  const textureA = createFloatMatrixTexture(matrixA);
  const textureB = createFloatMatrixTexture(matrixB);
  const outputTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, outputTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 16, 16, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, null);

  const framebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
  gl.framebufferTexture2D(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, outputTexture, 0);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
  gl.drawBuffers([gl.COLOR_ATTACHMENT0]);

  const vao = gl.createVertexArray();
  const positionBuffer = gl.createBuffer();
  gl.viewport(0, 0, 16, 16);
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]),
                gl.STATIC_DRAW);
  const positionLocation = gl.getAttribLocation(program, "a_position");
  assert(positionLocation >= 0, "GPGPU float position attribute not found");
  gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);
  gl.enableVertexAttribArray(positionLocation);

  gl.useProgram(program);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, textureA);
  gl.uniform1i(gl.getUniformLocation(program, "u_a"), 0);
  gl.activeTexture(gl.TEXTURE1);
  gl.bindTexture(gl.TEXTURE_2D, textureB);
  gl.uniform1i(gl.getUniformLocation(program, "u_b"), 1);
  gl.drawArrays(gl.TRIANGLES, 0, 3);

  const pixel = new Uint8Array(4);
  for (let row = 0; row < 4; ++row) {
    for (let col = 0; col < 4; ++col) {
      let expectedValue = 0;
      for (let k = 0; k < 4; ++k) {
        expectedValue += matrixA[row][k] * matrixB[k][col];
      }
      gl.readPixels(col, row, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
      const actualValue = pixel[0] / 255 * 4;
      assert(
          Math.abs(actualValue - expectedValue) <= 0.04,
          `GPGPU float matrix [${row}, ${col}] expected ${expectedValue}, ` +
              `got ${actualValue} from ${pixel[0]}`);
      assertPixelNear(
          new Uint8Array([pixel[1], pixel[2], pixel[3], 255]),
          [row * 48, col * 48, 255, 255],
          `GPGPU float matrix markers [${row}, ${col}]`, 1);
    }
  }

  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.bindVertexArray(null);
  gl.bindBuffer(gl.ARRAY_BUFFER, null);
  gl.activeTexture(gl.TEXTURE1);
  gl.bindTexture(gl.TEXTURE_2D, null);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, null);
  gl.deleteBuffer(positionBuffer);
  gl.deleteVertexArray(vao);
  gl.deleteFramebuffer(framebuffer);
  gl.deleteTexture(outputTexture);
  gl.deleteTexture(textureB);
  gl.deleteTexture(textureA);
  gl.deleteProgram(program);
  assertNoError(gl, "fragment shader GPGPU float matrix multiply");
}

function testFragmentShaderGpgpuUnsignedIntegerMatrixMultiply(gl) {
  const matrixA = [
    [1, 2, 0, 1],
    [0, 1, 3, 2],
    [2, 0, 1, 1],
    [1, 1, 1, 0]
  ];
  const matrixB = [
    [1, 0, 2, 1],
    [3, 1, 0, 2],
    [0, 2, 1, 1],
    [1, 1, 1, 0]
  ];

  function matrixToUnsignedTextureData(matrix) {
    const data = new Uint8Array(4 * 4 * 4);
    for (let row = 0; row < 4; ++row) {
      for (let col = 0; col < 4; ++col) {
        const offset = (row * 4 + col) * 4;
        data[offset] = matrix[row][col];
        data[offset + 3] = 1;
      }
    }
    return data;
  }

  function createUnsignedMatrixTexture(matrix) {
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    configureTexture(gl, gl.TEXTURE_2D);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8UI, 4, 4, 0, gl.RGBA_INTEGER,
                  gl.UNSIGNED_BYTE, matrixToUnsignedTextureData(matrix));
    return texture;
  }

  const program = createProgramFromSources(gl, `#version 300 es
precision highp float;
in vec2 a_position;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
}
`, `#version 300 es
precision highp float;
precision highp int;
precision highp usampler2D;
uniform usampler2D u_a;
uniform usampler2D u_b;
out vec4 out_color;
void main() {
  ivec2 cell = ivec2(gl_FragCoord.xy);
  if (cell.x >= 4 || cell.y >= 4) {
    out_color = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  uint sum = 0u;
  for (int k = 0; k < 4; ++k) {
    uint a = texelFetch(u_a, ivec2(k, cell.y), 0).r;
    uint b = texelFetch(u_b, ivec2(cell.x, k), 0).r;
    sum += a * b;
  }

  out_color = vec4(float(sum) * 16.0 / 255.0,
                   float(cell.y * 48) / 255.0,
                   float(cell.x * 48) / 255.0,
                   1.0);
}
`);
  const textureA = createUnsignedMatrixTexture(matrixA);
  const textureB = createUnsignedMatrixTexture(matrixB);
  const outputTexture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, outputTexture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 16, 16, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, null);

  const framebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
  gl.framebufferTexture2D(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, outputTexture, 0);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
  gl.drawBuffers([gl.COLOR_ATTACHMENT0]);

  const vao = gl.createVertexArray();
  const positionBuffer = gl.createBuffer();
  gl.viewport(0, 0, 16, 16);
  gl.bindVertexArray(vao);
  gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]),
                gl.STATIC_DRAW);
  const positionLocation = gl.getAttribLocation(program, "a_position");
  assert(positionLocation >= 0, "GPGPU unsigned position attribute not found");
  gl.vertexAttribPointer(positionLocation, 2, gl.FLOAT, false, 0, 0);
  gl.enableVertexAttribArray(positionLocation);

  gl.useProgram(program);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, textureA);
  gl.uniform1i(gl.getUniformLocation(program, "u_a"), 0);
  gl.activeTexture(gl.TEXTURE1);
  gl.bindTexture(gl.TEXTURE_2D, textureB);
  gl.uniform1i(gl.getUniformLocation(program, "u_b"), 1);
  gl.drawArrays(gl.TRIANGLES, 0, 3);

  const pixel = new Uint8Array(4);
  for (let row = 0; row < 4; ++row) {
    for (let col = 0; col < 4; ++col) {
      let expectedValue = 0;
      for (let k = 0; k < 4; ++k) {
        expectedValue += matrixA[row][k] * matrixB[k][col];
      }
      gl.readPixels(col, row, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
      assertPixelNear(
          pixel, [expectedValue * 16, row * 48, col * 48, 255],
          `GPGPU unsigned matrix [${row}, ${col}]`, 1);
    }
  }

  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.bindVertexArray(null);
  gl.bindBuffer(gl.ARRAY_BUFFER, null);
  gl.activeTexture(gl.TEXTURE1);
  gl.bindTexture(gl.TEXTURE_2D, null);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, null);
  gl.deleteBuffer(positionBuffer);
  gl.deleteVertexArray(vao);
  gl.deleteFramebuffer(framebuffer);
  gl.deleteTexture(outputTexture);
  gl.deleteTexture(textureB);
  gl.deleteTexture(textureA);
  gl.deleteProgram(program);
  assertNoError(gl, "fragment shader GPGPU unsigned integer matrix multiply");
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

function testExtensionAliasCalls(gl) {
  const vaoExt = gl.getExtension("OES_VERTEX_ARRAY_OBJECT");
  assert.notStrictEqual(vaoExt, null, "OES_vertex_array_object alias missing");
  assert.strictEqual(typeof vaoExt.createVertexArrayOES, "function");
  assert.strictEqual(typeof vaoExt.bindVertexArrayOES, "function");
  assert.strictEqual(typeof vaoExt.deleteVertexArrayOES, "function");
  assert.strictEqual(typeof vaoExt.isVertexArrayOES, "function");
  const vao = vaoExt.createVertexArrayOES();
  vaoExt.bindVertexArrayOES(vao);
  assert.strictEqual(vaoExt.isVertexArrayOES(vao), true);
  assert.strictEqual(gl.getParameter(vaoExt.VERTEX_ARRAY_BINDING_OES), vao);
  vaoExt.bindVertexArrayOES(null);
  vaoExt.deleteVertexArrayOES(vao);
  assertNoError(gl, "OES_vertex_array_object alias calls");

  const drawBuffersExt = gl.getExtension("WEBGL_DRAW_BUFFERS");
  assert.notStrictEqual(drawBuffersExt, null, "WEBGL_draw_buffers alias missing");
  assert.strictEqual(typeof drawBuffersExt.drawBuffersWEBGL, "function");
  assert.strictEqual(drawBuffersExt.COLOR_ATTACHMENT0_WEBGL,
                     gl.COLOR_ATTACHMENT0);
  assert.strictEqual(drawBuffersExt.DRAW_BUFFER0_WEBGL, gl.DRAW_BUFFER0);
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  configureTexture(gl, gl.TEXTURE_2D);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA,
                gl.UNSIGNED_BYTE, null);
  const framebuffer = gl.createFramebuffer();
  gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
  gl.framebufferTexture2D(
      gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
  assert.strictEqual(
      gl.checkFramebufferStatus(gl.FRAMEBUFFER), gl.FRAMEBUFFER_COMPLETE);
  drawBuffersExt.drawBuffersWEBGL([drawBuffersExt.COLOR_ATTACHMENT0_WEBGL]);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  gl.deleteFramebuffer(framebuffer);
  gl.deleteTexture(texture);
  assertNoError(gl, "WEBGL_draw_buffers alias calls");

  const instancedExt = gl.getExtension("ANGLE_INSTANCED_ARRAYS");
  assert.notStrictEqual(instancedExt, null, "ANGLE_instanced_arrays alias missing");
  assert.strictEqual(typeof instancedExt.drawArraysInstancedANGLE, "function");
  assert.strictEqual(typeof instancedExt.drawElementsInstancedANGLE, "function");
  assert.strictEqual(typeof instancedExt.vertexAttribDivisorANGLE, "function");
  testInstancedDrawing(gl, false, instancedExt);
  testInstancedDrawing(gl, true, instancedExt);
}

function testInstancedDrawing(gl, indexed, instancedExt = null) {
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
  if (instancedExt) {
    instancedExt.vertexAttribDivisorANGLE(offsetLocation, 1);
  } else {
    gl.vertexAttribDivisor(offsetLocation, 1);
  }

  if (indexed) {
    const elementBuffer = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, elementBuffer);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0]),
                  gl.STATIC_DRAW);
    if (instancedExt) {
      instancedExt.drawElementsInstancedANGLE(
          gl.POINTS, 1, gl.UNSIGNED_SHORT, 0, 2);
    } else {
      gl.drawElementsInstanced(gl.POINTS, 1, gl.UNSIGNED_SHORT, 0, 2);
    }
  } else {
    if (instancedExt) {
      instancedExt.drawArraysInstancedANGLE(gl.POINTS, 0, 1, 2);
    } else {
      gl.drawArraysInstanced(gl.POINTS, 0, 1, 2);
    }
  }

  const label = instancedExt ? "ANGLE_instanced_arrays alias" :
                               "WebGL2 instanced";
  assertRedPixel(gl, 4, 4, `${label} A`);
  assertRedPixel(gl, 12, 12, `${label} B`);
  assertNoError(gl, indexed ? `${label} drawElements` :
                              `${label} drawArrays`);
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
      gl.getError();
      const texture = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texImage2D(
          gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA,
          gl.UNSIGNED_BYTE, new Uint8Array([255, 0, 0, 255]));
      assert.strictEqual(gl.getError(), gl.NO_ERROR);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 1);
      gl.texImage2D(
          gl.TEXTURE_2D, 0, gl.RGBA, 1, 2, 0, gl.RGBA,
          gl.UNSIGNED_BYTE,
          new Uint8Array([255, 0, 0, 255, 0, 0, 255, 255]));
      assert.strictEqual(gl.getError(), gl.NO_ERROR);
    }
  `], {cwd: path.resolve(__dirname, ".."), stdio: "pipe"});
}

const gl = createContext();
console.log(gl.getParameter(gl.VERSION));
testRequiredWebGL2Methods(gl);
testSupportedExtensionsReflectGetExtension(gl);
testUnsupportedExtensionDoesNotWriteStderr();
testInfoLogEmptyStrings(gl);
testGetParameterSemantics(gl);
testBufferCopyAndReadback(gl);
testSamplerObjects(gl);
testSyncObjects(gl);
testTypedArrayTextureUploadOffsets(gl);
testPixelBufferObjectNumericOffsets(gl);
testMultisampleFramebufferOperations(gl);
testUnsignedIntegerUniforms(gl);
testNonSquareUniformMatrices(gl);
testExtensionAliasCalls(gl);
testWebGLOnlyPixelStore(gl);
testWebGLUnpackTransforms(gl);
testLayeredTextureOperations(gl);
testFragmentShaderGpgpuMatrixMultiply(gl);
testFragmentShaderGpgpuFloatMatrixMultiply(gl);
testFragmentShaderGpgpuUnsignedIntegerMatrixMultiply(gl);
testDeleteNullNoOps(gl);
testExtraArgumentsAreIgnored(gl);
testVertexArrayState(gl);
testInstancedDrawing(gl, false);
testInstancedDrawing(gl, true);
testWebGL2PixelStoreIsGatedForES2();
testDestroyApi();
testLoseContextApi();
testSequentialContextCleanup();
testLoseContextSequentialCleanup();
testFailedContextCreationDoesNotPoisonProcess();
testDeleteSyncUsesOwningContext();
testSyncFinalizerRestoresCurrentContext();
testMultipleLiveContextsUseOwnCurrentContext();
