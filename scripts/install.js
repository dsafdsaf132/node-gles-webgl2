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
const rmdir = util.promisify(fs.rmdir);
const readdir = util.promisify(fs.readdir);
const lstat = util.promisify(fs.lstat);
const unlink = util.promisify(fs.unlink);
const copyFile = util.promisify(fs.copyFile);
const readFile = util.promisify(fs.readFile);
const writeFile = util.promisify(fs.writeFile);

// Determine which archive to download based on the OS platform and arch:
const platform = os.platform().toLowerCase();
const arch = os.arch().toLowerCase();
if (arch !== 'x64' && arch !== 'arm64') {
  throw new Error(`The architecture ${arch} is not currently supported!`);
}
const platformArch = `${platform}-${arch}`;
const ANGLE_RELEASE_REPOSITORY =
    process.env.NODE_GLES_ANGLE_RELEASE_REPOSITORY ||
    'dsafdsaf132/angle-prebuilt';
const ANGLE_RELEASE_TAG =
    process.env.NODE_GLES_ANGLE_RELEASE_TAG || 'latest';
const ANGLE_LEGACY_VERSION = process.env.NODE_GLES_ANGLE_VERSION || '3729';
const useLegacyAngleUri =
    Boolean(process.env.NODE_GLES_ANGLE_VERSION ||
        process.env.NODE_GLES_ANGLE_BASE_URI);
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
const LEGACY_ANGLE_CACHE_ENV = 'NODE_GLES_ALLOW_LEGACY_ANGLE_CACHE';

// Dependency storage paths:
const depsPath = path.join(__dirname, '..', 'deps');
const anglePath = path.join(depsPath, 'angle');
const angleReleasePath = path.join(anglePath, 'out', 'Release');
const angleMetadataPath = path.join(anglePath, '.node-gles-angle.json');

//
// Ensures that a directory exists at a given path.
//
async function ensureDir(dirPath) {
  if (!await exists(dirPath)) {
    await mkdir(dirPath);
  }
}

async function unlinkIfExists(filePath) {
  try {
    await unlink(filePath);
  } catch (err) {
    if (!err || err.code !== 'ENOENT') {
      throw err;
    }
  }
}

async function lstatIfExists(filePath) {
  try {
    return await lstat(filePath);
  } catch (err) {
    if (err && err.code === 'ENOENT') {
      return null;
    }
    throw err;
  }
}

async function removePathIfExists(filePath) {
  const stat = await lstatIfExists(filePath);
  if (stat === null) {
    return;
  }

  if (stat.isDirectory() && !stat.isSymbolicLink()) {
    const entries = await readdir(filePath);
    for (const entry of entries) {
      await removePathIfExists(path.join(filePath, entry));
    }
    try {
      await rmdir(filePath);
    } catch (err) {
      if (!err || err.code !== 'ENOENT') {
        throw err;
      }
    }
    return;
  }

  await unlinkIfExists(filePath);
}

async function renameIfExists(source, destination) {
  if (await lstatIfExists(source) === null) {
    return;
  }

  await unlinkIfExists(destination);
  try {
    await rename(source, destination);
  } catch (err) {
    if (err && err.code === 'ENOENT' &&
        await lstatIfExists(source) === null) {
      return;
    }
    throw err;
  }
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
  const optionalRuntimeDlls = ['d3dcompiler_47.dll'];
  for (const dllName of optionalRuntimeDlls) {
    const source = path.join(angleReleasePath, dllName);
    if (await exists(source)) {
      await copyFile(source, path.join(buildReleasePath, dllName));
    }
  }
}

async function hasRequiredAngleFiles(angleDirPath) {
  angleDirPath = angleDirPath || anglePath;
  const releasePath = path.join(angleDirPath, 'out', 'Release');
  let requiredReleaseFiles;
  if (platform === 'win32') {
    requiredReleaseFiles = ['libEGL.dll', 'libGLESv2.dll', 'libEGL.lib',
      'libGLESv2.lib'];
  } else if (platform === 'darwin') {
    requiredReleaseFiles = ['libEGL.dylib', 'libGLESv2.dylib'];
  } else {
    requiredReleaseFiles = ['libEGL.so', 'libGLESv2.so'];
  }
  for (const fileName of requiredReleaseFiles) {
    if (!await exists(path.join(releasePath, fileName))) {
      return false;
    }
  }
  const requiredHeaders = [
    'include/EGL/egl.h',
    'include/EGL/eglext.h',
    'include/GLES2/gl2.h',
    'include/GLES2/gl2ext.h',
    'include/GLES3/gl3.h',
    'include/GLES3/gl32.h'
  ];
  for (const fileName of requiredHeaders) {
    if (!await exists(path.join(angleDirPath, fileName))) {
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

async function writeAngleMetadata(angleArchive) {
  const metadata = {
    provider: angleArchive.provider,
    version: angleArchive.version,
    releaseRepository: angleArchive.releaseRepository || null,
    releaseTag: angleArchive.releaseTag || null,
    requestedReleaseTag: angleArchive.requestedReleaseTag || null,
    platformArch: angleArchive.platformArch,
    archiveName: angleArchive.archiveName,
    archiveUri: angleArchive.archiveUri,
    sha256: process.env.NODE_GLES_ANGLE_SHA256 || null
  };
  await writeFile(angleMetadataPath, `${JSON.stringify(metadata, null, 2)}\n`);
}

function hasAngleOverrideEnv() {
  return Boolean(process.env.NODE_GLES_ANGLE_VERSION ||
      process.env.NODE_GLES_ANGLE_BASE_URI ||
      process.env.NODE_GLES_ANGLE_RELEASE_REPOSITORY ||
      process.env.NODE_GLES_ANGLE_RELEASE_TAG ||
      process.env.NODE_GLES_ANGLE_SHA256);
}

async function getReusableAngleFilesMessage(angleArchive) {
  if (!await hasRequiredAngleFiles()) {
    return null;
  }

  const metadata = await readAngleMetadata();
  if (metadata !== null) {
    if (metadata.provider === angleArchive.provider &&
        metadata.version === angleArchive.version &&
        metadata.releaseRepository ===
            (angleArchive.releaseRepository || null) &&
        metadata.releaseTag === (angleArchive.releaseTag || null) &&
        metadata.requestedReleaseTag ===
            (angleArchive.requestedReleaseTag || null) &&
        metadata.platformArch === angleArchive.platformArch &&
        metadata.archiveName === angleArchive.archiveName &&
        metadata.archiveUri === angleArchive.archiveUri &&
        metadata.sha256 === (process.env.NODE_GLES_ANGLE_SHA256 || null)) {
      return `* Reusing ANGLE from ${angleReleasePath}`;
    }
    return null;
  }

  if (process.env[LEGACY_ANGLE_CACHE_ENV] === '1') {
    if (hasAngleOverrideEnv()) {
      console.error(
          `* Ignoring ${LEGACY_ANGLE_CACHE_ENV} because an ANGLE override is set`);
      return null;
    }
    return `* Reusing metadata-less ANGLE cache from ${angleReleasePath}`;
  }

  return null;
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

function createHttpsOptions(uri, extraHeaders) {
  // If HTTPS_PROXY, https_proxy, HTTP_PROXY, or http_proxy is set
  const proxy = process.env['HTTPS_PROXY'] || process.env['https_proxy'] ||
      process.env['HTTP_PROXY'] || process.env['http_proxy'] || '';

  const angleUri = new URL(uri);
  const headers = Object.assign(
      {'Cache-Control': 'no-cache'}, extraHeaders || {});
  if (angleUri.hostname === 'api.github.com') {
    headers['User-Agent'] = 'node-gles-webgl2-installer';
    headers['Accept'] = 'application/vnd.github+json';
    const githubToken = process.env.NODE_GLES_GITHUB_TOKEN ||
        process.env.GITHUB_TOKEN;
    if (githubToken) {
      headers['Authorization'] = `Bearer ${githubToken}`;
    }
  }
  const options = {
    protocol: angleUri.protocol,
    hostname: angleUri.hostname,
    port: angleUri.port,
    path: `${angleUri.pathname}${angleUri.search}`,
    agent: https.globalAgent,
    headers
  };

  if (proxy !== '') {
    options.agent = new HttpsProxyAgent(proxy);
  }
  return options;
}

function requestJson(uri, redirectsRemaining = 5) {
  return new Promise((resolve, reject) => {
    const request = https.get(createHttpsOptions(uri), response => {
      if (response.statusCode >= 300 && response.statusCode < 400 &&
          response.headers.location) {
        response.resume();
        if (redirectsRemaining <= 0) {
          reject(new Error(`Too many redirects: ${uri}`));
          return;
        }
        const redirectedUri =
            new URL(response.headers.location, uri).toString();
        requestJson(redirectedUri, redirectsRemaining - 1)
            .then(resolve, reject);
        return;
      }

      let body = '';
      response.setEncoding('utf8');
      response.on('data', chunk => body += chunk);
      response.on('end', () => {
        if (response.statusCode < 200 || response.statusCode >= 300) {
          reject(new Error(
              `Failed to query ANGLE release: HTTP ` +
              `${response.statusCode} ${uri}\n${body}`));
          return;
        }
        try {
          resolve(JSON.parse(body));
        } catch (err) {
          reject(err);
        }
      });
      response.on('error', reject);
    });

    request.on('error', reject);
    request.setTimeout(60000, () => {
      request.destroy(new Error(`Timed out querying ANGLE release from ${uri}`));
    });
  });
}

function createLegacyAngleArchive() {
  const configuredAngleBaseUri =
      process.env.NODE_GLES_ANGLE_BASE_URI ||
      'https://storage.googleapis.com/angle-builds/';
  const angleBinaryBaseUri = configuredAngleBaseUri.endsWith('/') ?
      configuredAngleBaseUri :
      `${configuredAngleBaseUri}/`;
  const archiveName =
      `angle-${ANGLE_LEGACY_VERSION}-${platformArch}.${archiveExtension}`;
  return {
    provider: 'archive-uri',
    version: ANGLE_LEGACY_VERSION,
    releaseRepository: null,
    releaseTag: null,
    requestedReleaseTag: null,
    platformArch,
    archiveName,
    archiveUri: new URL(archiveName, angleBinaryBaseUri).toString()
  };
}

async function resolveGitHubAngleArchive() {
  const releasePath = ANGLE_RELEASE_TAG === 'latest' ?
      'latest' :
      `tags/${encodeURIComponent(ANGLE_RELEASE_TAG)}`;
  const releaseUri =
      `https://api.github.com/repos/${ANGLE_RELEASE_REPOSITORY}/releases/` +
      releasePath;
  const release = await requestJson(releaseUri);
  const releaseTag = release.tag_name;
  const expectedArchiveName =
      `angle-${releaseTag}-${platformArch}.${archiveExtension}`;
  const expectedArchiveSuffix =
      `-${platformArch}.${archiveExtension}`;
  const assets = Array.isArray(release.assets) ? release.assets : [];
  const asset = assets.find(item => item.name === expectedArchiveName) ||
      assets.find(item => item.name && item.name.endsWith(expectedArchiveSuffix));
  if (!asset) {
    const availableAssets = assets.map(item => item.name).join(', ') || '(none)';
    throw new Error(
        `ANGLE release ${releaseTag} does not provide an archive for ` +
        `${platformArch}. Expected ${expectedArchiveName}. ` +
        `Available assets: ${availableAssets}`);
  }
  const archiveUri = asset.browser_download_url || asset.url;
  return {
    provider: 'github-release',
    version: releaseTag,
    releaseRepository: ANGLE_RELEASE_REPOSITORY,
    releaseTag,
    requestedReleaseTag: ANGLE_RELEASE_TAG,
    platformArch,
    archiveName: asset.name,
    archiveUri
  };
}

async function resolveAngleArchive() {
  if (useLegacyAngleUri) {
    return createLegacyAngleArchive();
  }
  return resolveGitHubAngleArchive();
}

function downloadFile(uri, tempFileName, redirectsRemaining = 5) {
  return new Promise((resolve, reject) => {
    const request = https.get(createHttpsOptions(uri), response => {
      if (response.statusCode >= 300 && response.statusCode < 400 &&
          response.headers.location) {
        response.resume();
        if (redirectsRemaining <= 0) {
          reject(new Error(`Too many ANGLE download redirects: ${uri}`));
          return;
        }
        const redirectedUri =
            new URL(response.headers.location, uri).toString();
        downloadFile(redirectedUri, tempFileName, redirectsRemaining - 1)
            .then(resolve, reject);
        return;
      }

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
// Downloads and extracts the selected ANGLE archive.
//
async function downloadAngleLibs() {
  const angleArchive = await resolveAngleArchive();
  const reusableAngleFilesMessage =
      await getReusableAngleFilesMessage(angleArchive);
  if (reusableAngleFilesMessage !== null) {
    console.error(reusableAngleFilesMessage);
    return;
  }

  console.error(
      `* Downloading ANGLE ${angleArchive.version} for ` +
      `${angleArchive.platformArch}`);
  await ensureDir(depsPath);

  const tempFileName =
      path.join(__dirname, `_tmp-angle-${process.pid}.${archiveExtension}`);
  const stagingDepsPath = path.join(depsPath, `_tmp-angle-${process.pid}`);
  const stagingAnglePath = path.join(stagingDepsPath, 'angle');
  const stagingAngleReleasePath =
      path.join(stagingAnglePath, 'out', 'Release');
  try {
    await removePathIfExists(stagingDepsPath);
    await mkdir(stagingDepsPath);
    await downloadFileWithRetries(angleArchive.archiveUri, tempFileName);
    await verifyArchiveChecksum(tempFileName);
    if (platform === 'win32') {
      const zipFile = new zip(tempFileName);
      assertSafeZipEntries(zipFile);
      zipFile.extractAllTo(stagingDepsPath, true /* overwrite */);

      // The .lib files for the two .dll files we care about have a name the
      // compiler doesn't like - rename them when present.
      await renameIfExists(
          path.join(stagingAngleReleasePath, 'libGLESv2.dll.lib'),
          path.join(stagingAngleReleasePath, 'libGLESv2.lib'));
      await renameIfExists(
          path.join(stagingAngleReleasePath, 'libEGL.dll.lib'),
          path.join(stagingAngleReleasePath, 'libEGL.lib'));
    } else {
      assertSafeTarEntries(tempFileName);
      cp.execFileSync(
          'tar', ['-xzf', tempFileName, '-C', stagingDepsPath],
          {stdio: 'inherit'});
    }
    if (!await hasRequiredAngleFiles(stagingAnglePath)) {
      throw new Error(
          `ANGLE archive did not provide the required files in ` +
          angleReleasePath);
    }
    await removePathIfExists(anglePath);
    await rename(stagingAnglePath, anglePath);
    await writeAngleMetadata(angleArchive);
  } finally {
    await unlinkIfExists(tempFileName);
    await removePathIfExists(stagingDepsPath);
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
