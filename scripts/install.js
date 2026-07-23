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
        ['libEGL.dll', 'libGLESv2.dll', 'libEGL.lib', 'libGLESv2.lib'];
  } else if (platform === 'darwin') {
    libraries = ['libEGL.dylib', 'libGLESv2.dylib'];
  } else {
    libraries = ['libEGL.so', 'libGLESv2.so'];
  }
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
  console.error(`* Using bundled ANGLE for ${platform}-${arch}`);
}

function copyRequiredWindowsDlls() {
  if (platform !== 'win32') {
    return;
  }
  const buildReleasePath = path.join(__dirname, '..', 'build', 'Release');
  fs.mkdirSync(buildReleasePath, {recursive: true});
  for (const dllName of
    ['libEGL.dll', 'libGLESv2.dll', 'd3dcompiler_47.dll']) {
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
    console.error('* node-gyp failed, retrying with npx node-gyp');
    cp.execSync('npx --yes node-gyp rebuild', {stdio: 'inherit'});
  }
  copyRequiredWindowsDlls();
}

validateBundledAngle();
buildBindings();
