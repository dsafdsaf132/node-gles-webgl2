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
  assert.deepStrictEqual(gl.getParameter(gl.COLOR_WRITEMASK),
                         [true, true, true, true]);
  assert.strictEqual(typeof gl.getParameter(gl.DEPTH_WRITEMASK), "boolean");
  assert(Array.isArray(gl.getParameter(gl.ALIASED_LINE_WIDTH_RANGE)));
  assert(Array.isArray(gl.getParameter(gl.COMPRESSED_TEXTURE_FORMATS)));

  assert.strictEqual(gl.getParameter(gl.ARRAY_BUFFER_BINDING), 0);
  assert.strictEqual(gl.getParameter(gl.CURRENT_PROGRAM), 0);
  assert.strictEqual(gl.getParameter(gl.VERTEX_ARRAY_BINDING), 0);
  assert.strictEqual(gl.getParameter(gl.SAMPLER_BINDING), 0);
  assert(Number.isInteger(gl.getParameter(gl.MAX_3D_TEXTURE_SIZE)));
  assert(Number.isInteger(gl.getParameter(gl.MAX_ARRAY_TEXTURE_LAYERS)));
  assert(Number.isInteger(gl.getParameter(gl.MAX_TEXTURE_SIZE)));
  const maxDrawBuffers = gl.getParameter(gl.MAX_DRAW_BUFFERS);
  assert(Number.isInteger(maxDrawBuffers));
  assert.strictEqual(gl.getParameter(gl.DRAW_BUFFER0), gl.BACK);
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
  gl.clearColor(0.25, 0.5, 0.75, 1.0);
  gl.clear(gl.COLOR_BUFFER_BIT);
  const packBuffer = gl.createBuffer();
  gl.bindBuffer(gl.PIXEL_PACK_BUFFER, packBuffer);
  gl.bufferData(gl.PIXEL_PACK_BUFFER, new Uint8Array(8), gl.STREAM_READ);
  gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, 4);
  const packed = new Uint8Array(8);
  gl.getBufferSubData(gl.PIXEL_PACK_BUFFER, 0, packed);
  assert.deepStrictEqual(Array.from(packed.subarray(0, 4)), [0, 0, 0, 0]);
  assertPixelNear(
      packed.subarray(4, 8), [64, 128, 191, 255],
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
