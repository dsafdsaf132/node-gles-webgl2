"use strict";

const assert = require("assert");
const {execFileSync} = require("child_process");

if (process.argv.includes("--child")) {
  const gl = require("..").createWebGLRenderingContext({width: 4, height: 4});
  try {
    const info = gl.getExtension("WEBGL_debug_renderer_info");
    assert(info, "renderer information must be available");
    const renderer = gl.getParameter(info.UNMASKED_RENDERER_WEBGL);
    assert(/SwiftShader/i.test(renderer), renderer);
    gl.clearColor(1, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);

    // Exercise shader compilation, pipeline creation and JIT execution too.
    const shaders = [];
    for (const [type, source] of [
      [gl.VERTEX_SHADER, `#version 300 es
const vec2 positions[3] = vec2[3](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));
void main() { gl_Position = vec4(positions[gl_VertexID], 0, 1); }`],
      [gl.FRAGMENT_SHADER, `#version 300 es
precision mediump float;
out vec4 color;
void main() { color = vec4(0, 1, 0, 1); }`]
    ]) {
      const shader = gl.createShader(type);
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      assert.strictEqual(gl.getShaderParameter(shader, gl.COMPILE_STATUS), true,
                         gl.getShaderInfoLog(shader));
      shaders.push(shader);
    }
    const program = gl.createProgram();
    for (const shader of shaders) gl.attachShader(program, shader);
    gl.linkProgram(program);
    assert.strictEqual(gl.getProgramParameter(program, gl.LINK_STATUS), true,
                       gl.getProgramInfoLog(program));
    gl.useProgram(program);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    const pixel = new Uint8Array(4);
    gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
    assert.deepStrictEqual(Array.from(pixel), [0, 255, 0, 255]);
    assert.strictEqual(gl.getError(), gl.NO_ERROR);
    gl.useProgram(null);
    gl.deleteProgram(program);
    for (const shader of shaders) gl.deleteShader(shader);
    console.log(`SwiftShader headless smoke passed: ${renderer}`);
  } finally {
    gl.destroy();
  }
} else {
  // Use a fresh process for each backend selection, with no window server.
  for (const platform of ["swiftshader", "SWIFTSHADER"]) {
    const env = Object.assign({}, process.env, {ANGLE_DEFAULT_PLATFORM: platform});
    delete env.DISPLAY;
    delete env.WAYLAND_DISPLAY;
    execFileSync(process.execPath, [__filename, "--child"], {env, stdio: "inherit"});
  }
}
