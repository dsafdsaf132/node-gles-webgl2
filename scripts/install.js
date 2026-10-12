/**
 * @license
 * Copyright 2019 Google Inc. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * =============================================================================
 */

const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const resolveAnglePath = require('./resolve-angle-path');

const platform = os.platform().toLowerCase();
const arch = os.arch().toLowerCase();
const supportedPlatforms = new Set(['darwin', 'linux', 'win32']);
const supportedArchitectures = new Set(['x64', 'arm64']);

if (!supportedPlatforms.has(platform)) {
  throw new Error(`The platform ${platform} is not supported`);
}
if (!supportedArchitectures.has(arch)) {
  throw new Error(`The architecture ${arch} is not supported`);
}

const anglePath = path.join(__dirname, '..', 'deps', 'angle');
const angleReleasePath = resolveAnglePath();

function requiredAngleFiles() {
  const headers = [
    'include/EGL/egl.h',
    'include/EGL/eglext.h',
    'include/GLES2/gl2.h',
    'include/GLES2/gl2ext.h',
    'include/GLES3/gl3.h',
    'include/GLES3/gl32.h'
  ];
  let libraries;
  if (platform === 'win32') {
    libraries =
        ['libEGL.dll', 'libGLESv2.dll', 'libEGL.lib', 'libGLESv2.lib',
         'vk_swiftshader.dll', 'vulkan-1.dll'];
  } else if (platform === 'darwin') {
    libraries = ['libEGL.dylib', 'libGLESv2.dylib', 'libvk_swiftshader.dylib'];
  } else {
    libraries = ['libEGL.so', 'libGLESv2.so', 'libvk_swiftshader.so'];
  }
  libraries.push('vk_swiftshader_icd.json');
  return headers.map(file => path.join(anglePath, file))
      .concat(libraries.map(file => path.join(angleReleasePath, file)));
}

function validateBundledAngle() {
  const missingFiles = requiredAngleFiles().filter(file => !fs.existsSync(file));
  if (missingFiles.length !== 0) {
    throw new Error(
        `Bundled ANGLE files are missing for ${platform}-${arch}:\n` +
        missingFiles.map(file => `  ${file}`).join('\n'));
  }
  if (platform === 'linux') {
    for (const name of ['libEGL.so', 'libGLESv2.so', 'libvk_swiftshader.so']) {
      const filename = path.join(angleReleasePath, name);
      const header = Buffer.alloc(20);
      const fd = fs.openSync(filename, 'r');
      let bytesRead;
      try {
        bytesRead = fs.readSync(fd, header, 0, header.length, 0);
      } finally {
        fs.closeSync(fd);
      }
      const machine = arch === 'x64' ? 62 : 183;
      if (bytesRead !== header.length ||
          header.readUInt32BE(0) !== 0x7f454c46 ||
          header[4] !== 2 || header[5] !== 1 || header[6] !== 1 ||
          header.readUInt16LE(16) !== 3 ||
          header.readUInt16LE(18) !== machine) {
        throw new Error(
            `Invalid ANGLE library for linux-${arch}: ${filename}. ` +
            'Expected a 64-bit little-endian ELF shared library. ' +
            'Reinstall the platform package or restore the bundled libraries.');
      }
    }
  }
  console.error(`* Using bundled ANGLE for ${platform}-${arch}`);
}

function copyRequiredWindowsDlls() {
  if (platform !== 'win32') {
    return;
  }
  const buildReleasePath = path.join(__dirname, '..', 'build', 'Release');
  fs.mkdirSync(buildReleasePath, {recursive: true});
  for (const dllName of
    ['libEGL.dll', 'libGLESv2.dll', 'd3dcompiler_47.dll',
     'vulkan-1.dll', 'vk_swiftshader.dll', 'vk_swiftshader_icd.json']) {
    const source = path.join(angleReleasePath, dllName);
    if (fs.existsSync(source)) {
      fs.copyFileSync(source, path.join(buildReleasePath, dllName));
    }
  }
}

function buildBindings() {
  console.error('* Building ANGLE bindings');
  try {
    cp.execSync('node-gyp rebuild', {stdio: 'inherit'});
  } catch (err) {
    // Keep the fallback compatible with the supported Node releases rather
    // than downloading a future node-gyp with a higher Node requirement.
    const [nodeMajor, nodeMinor] = process.versions.node.split('.').map(Number);
    // node-gyp 12 supports Visual Studio 2026 and requires Node 20.17 or
    // 22.9+. Older runtimes must retain a compatible fallback.
    const supportsGyp12 = nodeMajor >= 23 ||
        (nodeMajor === 22 && nodeMinor >= 9) ||
        (nodeMajor === 20 && nodeMinor >= 17);
    const gypVersion = supportsGyp12 ? '12.3.0' :
        nodeMajor >= 18 || (nodeMajor === 16 && nodeMinor >= 14) ?
        '10.3.1' : '8.4.1';
    console.error(`* node-gyp failed, retrying with node-gyp@${gypVersion}`);
    cp.execSync(`npx --yes node-gyp@${gypVersion} rebuild`, {stdio: 'inherit'});
  }
  copyRequiredWindowsDlls();
}

validateBundledAngle();
buildBindings();
