"use strict";

const assert = require("assert");
const THREE = require("three");
const nodeGles = require("..");

const WIDTH = 96;
const HEIGHT = 48;

function createContext() {
  return nodeGles.createWebGLRenderingContext({
    width: WIDTH,
    height: HEIGHT,
    majorVersion: 3,
    minorVersion: 0
  });
}

function createCanvas(gl) {
  const canvas = {
    width: WIDTH,
    height: HEIGHT,
    style: {},
    addEventListener() {},
    removeEventListener() {},
    getContext(type) {
      if (type === "webgl2" || type === "webgl" ||
          type === "experimental-webgl") {
        return gl;
      }
      return null;
    }
  };
  gl.canvas = canvas;
  return canvas;
}

function readDefaultPixel(gl, x, y) {
  const pixel = new Uint8Array(4);
  gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
  return pixel;
}

function assertPixelNear(pixel, expected, label, tolerance = 8) {
  for (let i = 0; i < 4; ++i) {
    assert(
        Math.abs(pixel[i] - expected[i]) <= tolerance,
        `${label}: channel ${i} expected ${expected[i]}, got ${pixel[i]}`);
  }
}

function assertSamples(gl, samples) {
  for (const sample of samples) {
    assertPixelNear(
        readDefaultPixel(gl, sample.x, sample.y), sample.expected,
        sample.label);
  }
}

function makeTriangle() {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array([
    -0.72, -0.72, 0,
    0.72, -0.72, 0,
    0.0, 0.78, 0
  ]), 3));
  const material = new THREE.MeshBasicMaterial({color: 0x2f6fff});
  const mesh = new THREE.Mesh(geometry, material);
  mesh.position.x = -2;
  return {geometry, material, mesh};
}

function makeCircle() {
  const geometry = new THREE.CircleGeometry(0.62, 40);
  const material = new THREE.MeshBasicMaterial({color: 0x21d66b});
  const mesh = new THREE.Mesh(geometry, material);
  return {geometry, material, mesh};
}

function makeBox() {
  const geometry = new THREE.BoxGeometry(1.05, 1.05, 1.05);
  const material = new THREE.MeshBasicMaterial({color: 0xff4b3e});
  const mesh = new THREE.Mesh(geometry, material);
  mesh.position.x = 2;
  mesh.rotation.x = 0.45;
  mesh.rotation.y = 0.6;
  return {geometry, material, mesh};
}

function disposeDrawable(drawable) {
  drawable.geometry.dispose();
  drawable.material.dispose();
}

function testThreeShapeRendering() {
  const gl = createContext();
  let renderer = null;
  const drawables = [];

  try {
    renderer = new THREE.WebGLRenderer({
      canvas: createCanvas(gl),
      context: gl,
      alpha: true,
      antialias: false
    });
    renderer.setSize(WIDTH, HEIGHT, false);
    renderer.setClearColor(0x111111, 1);

    const scene = new THREE.Scene();
    const camera = new THREE.OrthographicCamera(-3, 3, 1.5, -1.5, 0.1, 10);
    camera.position.z = 5;

    drawables.push(makeTriangle(), makeCircle(), makeBox());
    for (const drawable of drawables) {
      scene.add(drawable.mesh);
    }

    renderer.render(scene, camera);

    assertSamples(gl, [
      {x: 16, y: 18, expected: [47, 111, 255, 255], label: "triangle lower"},
      {x: 16, y: 24, expected: [47, 111, 255, 255], label: "triangle center"},
      {x: 16, y: 30, expected: [47, 111, 255, 255], label: "triangle upper"},
      {x: 42, y: 24, expected: [33, 214, 107, 255], label: "circle left"},
      {x: 48, y: 24, expected: [33, 214, 107, 255], label: "circle center"},
      {x: 54, y: 24, expected: [33, 214, 107, 255], label: "circle right"},
      {x: 76, y: 24, expected: [255, 75, 62, 255], label: "box left"},
      {x: 80, y: 24, expected: [255, 75, 62, 255], label: "box center"},
      {x: 84, y: 24, expected: [255, 75, 62, 255], label: "box right"},
      {x: 2, y: 2, expected: [17, 17, 17, 255], label: "background corner"},
      {x: 32, y: 44, expected: [17, 17, 17, 255], label: "background gap"}
    ]);
    assert.strictEqual(gl.getError(), gl.NO_ERROR, "three smoke GL error");
  } finally {
    if (renderer !== null) {
      renderer.dispose();
    }
    for (const drawable of drawables) {
      disposeDrawable(drawable);
    }
    gl.destroy();
  }
}

testThreeShapeRendering();
