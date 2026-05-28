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
const crypto = require('crypto');
const fs = require('fs');
const https = require('https');
const path = require('path');
const util = require('util');
const os = require('os');
const URL = require('url').URL;
const zip = require('adm-zip');
const HttpsProxyAgent = require('https-proxy-agent');
const ProgressBar = require('progress');

const mkdir = util.promisify(fs.mkdir);
const exists = util.promisify(fs.exists);
const rename = util.promisify(fs.rename);
const unlink = util.promisify(fs.unlink);
const copyFile = util.promisify(fs.copyFile);
const readFile = util.promisify(fs.readFile);
const writeFile = util.promisify(fs.writeFile);

// Determine which tarball to download based on the OS platform and arch:
const platform = os.platform().toLowerCase();
const platformArch = `${platform}-${os.arch().toLowerCase()}`;
const ANGLE_VERSION = process.env.NODE_GLES_ANGLE_VERSION || '3729';
const configuredAngleBaseUri =
    process.env.NODE_GLES_ANGLE_BASE_URI ||
    'https://storage.googleapis.com/angle-builds/';
const ANGLE_BINARY_BASE_URI = configuredAngleBaseUri.endsWith('/') ?
    configuredAngleBaseUri :
    `${configuredAngleBaseUri}/`;
let archiveExtension;
if (platform === 'darwin') {
  archiveExtension = 'tar.gz';
} else if (platform === 'linux') {
  archiveExtension = 'tar.gz';
} else if (platform === 'win32') {
  archiveExtension = 'zip';
} else {
  console.log('platform: ' + platform);
  throw new Error(`The platform ${platformArch} is not currently supported!`);
}
const ANGLE_ARCHIVE_NAME =
    `angle-${ANGLE_VERSION}-${platformArch}.${archiveExtension}`;
const ANGLE_BINARY_URI = new URL(
    ANGLE_ARCHIVE_NAME, ANGLE_BINARY_BASE_URI).toString();

// Dependency storage paths:
const depsPath = path.join(__dirname, '..', 'deps');
const angleReleasePath = path.join(depsPath, 'angle', 'out', 'Release');
const angleMetadataPath = path.join(depsPath, 'angle', '.node-gles-angle.json');

//
// Ensures that a directory exists at a given path.
//
async function ensureDir(dirPath) {
  if (!await exists(dirPath)) {
    await mkdir(dirPath);
  }
}

async function unlinkIfExists(filePath) {
  if (await exists(filePath)) {
    await unlink(filePath);
  }
}

async function renameIfExists(source, destination) {
  if (!await exists(source)) {
    return;
  }
  await unlinkIfExists(destination);
  await rename(source, destination);
}

async function copyRequiredWindowsDlls() {
  if (platform !== 'win32') {
    return;
  }

  const buildReleasePath = path.join(__dirname, '..', 'build', 'Release');
  await ensureDir(buildReleasePath);
  for (const dllName of ['libEGL.dll', 'libGLESv2.dll']) {
    const source = path.join(angleReleasePath, dllName);
    if (!await exists(source)) {
      throw new Error(`Missing required ANGLE runtime DLL: ${source}`);
    }
    await copyFile(source, path.join(buildReleasePath, dllName));
  }
}

async function hasRequiredAngleFiles() {
  let requiredFiles;
  if (platform === 'win32') {
    requiredFiles = ['libEGL.dll', 'libGLESv2.dll', 'libEGL.lib',
      'libGLESv2.lib'];
  } else if (platform === 'darwin') {
    requiredFiles = ['libEGL.dylib', 'libGLESv2.dylib'];
  } else {
    requiredFiles = ['libEGL.so', 'libGLESv2.so'];
  }
  for (const fileName of requiredFiles) {
    if (!await exists(path.join(angleReleasePath, fileName))) {
      return false;
    }
  }
  return true;
}

async function readAngleMetadata() {
  if (!await exists(angleMetadataPath)) {
    return null;
  }
  try {
    const content = await readFile(angleMetadataPath, 'utf8');
    return JSON.parse(content);
  } catch (err) {
    return null;
  }
}

async function writeAngleMetadata() {
  const metadata = {
    version: ANGLE_VERSION,
    platformArch,
    archiveName: ANGLE_ARCHIVE_NAME,
    archiveUri: ANGLE_BINARY_URI,
    sha256: process.env.NODE_GLES_ANGLE_SHA256 || null
  };
  await writeFile(angleMetadataPath, `${JSON.stringify(metadata, null, 2)}\n`);
}

async function hasReusableAngleFiles() {
  if (!await hasRequiredAngleFiles()) {
    return false;
  }

  const metadata = await readAngleMetadata();
  if (metadata !== null) {
    return metadata.version === ANGLE_VERSION &&
        metadata.platformArch === platformArch &&
        metadata.archiveName === ANGLE_ARCHIVE_NAME &&
        metadata.archiveUri === ANGLE_BINARY_URI &&
        metadata.sha256 === (process.env.NODE_GLES_ANGLE_SHA256 || null);
  }

  const hasInstallOverride = Boolean(process.env.NODE_GLES_ANGLE_VERSION) ||
      Boolean(process.env.NODE_GLES_ANGLE_BASE_URI) ||
      Boolean(process.env.NODE_GLES_ANGLE_SHA256);
  return !hasInstallOverride;
}

function assertSafeTarEntries(tempFileName) {
  const listing = cp.execFileSync('tar', ['-tzf', tempFileName], {
    encoding: 'utf8'
  });

  const entries = listing.split(/\r?\n/).filter(Boolean);
  for (const entry of entries) {
    const normalized = path.posix.normalize(entry);
    if (path.posix.isAbsolute(entry) || normalized === '..' ||
        normalized.startsWith('../') ||
        (normalized !== 'angle' && !normalized.startsWith('angle/'))) {
      throw new Error(`Unexpected ANGLE archive entry: ${entry}`);
    }
  }
}

function assertSafeZipEntries(zipFile) {
  for (const entry of zipFile.getEntries()) {
    const entryName = entry.entryName.replace(/\\/g, '/');
    const normalized = path.posix.normalize(entryName);
    if (path.posix.isAbsolute(entryName) || normalized === '..' ||
        normalized.startsWith('../') ||
        (normalized !== 'angle' && !normalized.startsWith('angle/'))) {
      throw new Error(`Unexpected ANGLE archive entry: ${entry.entryName}`);
    }
  }
}

function createHttpsOptions(uri) {
  // If HTTPS_PROXY, https_proxy, HTTP_PROXY, or http_proxy is set
  const proxy = process.env['HTTPS_PROXY'] || process.env['https_proxy'] ||
      process.env['HTTP_PROXY'] || process.env['http_proxy'] || '';

  const angleUri = new URL(uri);
  const options = {
    protocol: angleUri.protocol,
    hostname: angleUri.hostname,
    port: angleUri.port,
    path: `${angleUri.pathname}${angleUri.search}`,
    agent: https.globalAgent,
    headers: {'Cache-Control': 'no-cache'}
  };

  if (proxy !== '') {
    options.agent = new HttpsProxyAgent(proxy);
  }
  return options;
}

function downloadFile(uri, tempFileName) {
  return new Promise((resolve, reject) => {
    const request = https.get(createHttpsOptions(uri), response => {
      if (response.statusCode < 200 || response.statusCode >= 300) {
        response.resume();
        reject(new Error(
            `Failed to download ANGLE: HTTP ${response.statusCode} ${uri}`));
        return;
      }

      const total = parseInt(response.headers['content-length'], 10);
      const bar = Number.isFinite(total) && total > 0 ?
          new ProgressBar('[:bar] :rate/bps :percent :etas', {
            complete: '=',
            incomplete: ' ',
            width: 30,
            total
          }) :
          null;

      const outputFile = fs.createWriteStream(tempFileName);
      response.on('data', chunk => {
        if (bar !== null) {
          bar.tick(chunk.length);
        }
      });
      response.on('error', reject);
      outputFile.on('error', reject);
      outputFile.on('finish', () => outputFile.close(resolve));
      response.pipe(outputFile);
    });

    request.on('error', reject);
    request.setTimeout(60000, () => {
      request.destroy(new Error(`Timed out downloading ANGLE from ${uri}`));
    });
  });
}

async function downloadFileWithRetries(uri, tempFileName) {
  const attempts = 3;
  let lastError;
  for (let attempt = 1; attempt <= attempts; ++attempt) {
    try {
      await unlinkIfExists(tempFileName);
      await downloadFile(uri, tempFileName);
      return;
    } catch (err) {
      lastError = err;
      await unlinkIfExists(tempFileName);
      if (attempt < attempts) {
        console.error(
            `* ANGLE download failed, retrying (${attempt}/${attempts}): ` +
            err.message);
      }
    }
  }
  throw lastError;
}

function sha256File(filePath) {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha256');
    const stream = fs.createReadStream(filePath);
    stream.on('data', chunk => hash.update(chunk));
    stream.on('error', reject);
    stream.on('end', () => resolve(hash.digest('hex')));
  });
}

async function verifyArchiveChecksum(tempFileName) {
  const expected = process.env.NODE_GLES_ANGLE_SHA256;
  if (!expected) {
    return;
  }
  const actual = await sha256File(tempFileName);
  if (actual.toLowerCase() !== expected.toLowerCase()) {
    throw new Error(
        `ANGLE archive checksum mismatch: expected ${expected}, got ${actual}`);
  }
}

//
// Downloads and extracts the ANGLE archive set at `ANGLE_BINARY_URI`.
//
async function downloadAngleLibs() {
  if (await hasReusableAngleFiles()) {
    console.error(`* Reusing ANGLE from ${angleReleasePath}`);
    return;
  }

  console.error(`* Downloading ANGLE ${ANGLE_VERSION} for ${platformArch}`);
  await ensureDir(depsPath);

  const tempFileName =
      path.join(__dirname, `_tmp-angle-${process.pid}.${archiveExtension}`);
  try {
    await downloadFileWithRetries(ANGLE_BINARY_URI, tempFileName);
    await verifyArchiveChecksum(tempFileName);
    if (platform === 'win32') {
      const zipFile = new zip(tempFileName);
      assertSafeZipEntries(zipFile);
      zipFile.extractAllTo(depsPath, true /* overwrite */);

      // The .lib files for the two .dll files we care about have a name the
      // compiler doesn't like - rename them when present.
      await renameIfExists(
          path.join(angleReleasePath, 'libGLESv2.dll.lib'),
          path.join(angleReleasePath, 'libGLESv2.lib'));
      await renameIfExists(
          path.join(angleReleasePath, 'libEGL.dll.lib'),
          path.join(angleReleasePath, 'libEGL.lib'));
    } else {
      assertSafeTarEntries(tempFileName);
      cp.execFileSync(
          'tar', ['-xzf', tempFileName, '-C', depsPath], {stdio: 'inherit'});
    }
    if (!await hasRequiredAngleFiles()) {
      throw new Error(
          `ANGLE archive did not provide the required files in ` +
          angleReleasePath);
    }
    await writeAngleMetadata();
  } finally {
    await unlinkIfExists(tempFileName);
  }
}

//
// Wraps and executes a node-gyp rebuild command.
//
async function buildBindings() {
  console.error('* Building ANGLE bindings');
  try {
    cp.execSync('node-gyp rebuild', {stdio: 'inherit'});
  } catch (err) {
    console.error('* node-gyp failed, retrying with npx node-gyp');
    cp.execSync('npx --yes node-gyp rebuild', {stdio: 'inherit'});
  }
  await copyRequiredWindowsDlls();
}

//
// Main execution function for this script.
//
async function run() {
  await downloadAngleLibs();
  await buildBindings();
}

run().catch(err => {
  console.error(err && err.stack ? err.stack : err);
  process.exit(1);
});
