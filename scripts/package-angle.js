#!/usr/bin/env node

const cp = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const AdmZip = require('adm-zip');

function usage() {
  console.error(
      'Usage: node scripts/package-angle.js --source <angle-dir> ' +
      '--version <archive-version> --out-dir <output-dir>');
}

function parseArgs(argv) {
  const args = {};
  for (let i = 2; i < argv.length; i += 2) {
    const name = argv[i];
    const value = argv[i + 1];
    if (!name || !name.startsWith('--') || value === undefined) {
      usage();
      process.exit(1);
    }
    args[name.slice(2)] = value;
  }
  return args;
}

function copyRecursive(source, destination) {
  const stat = fs.lstatSync(source);
  if (stat.isDirectory() && !stat.isSymbolicLink()) {
    fs.mkdirSync(destination, {recursive: true});
    for (const entry of fs.readdirSync(source)) {
      copyRecursive(path.join(source, entry), path.join(destination, entry));
    }
    return;
  }

  fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.copyFileSync(source, destination);
}

function addDirectoryToZip(zipFile, directory, zipPrefix) {
  for (const entry of fs.readdirSync(directory)) {
    const source = path.join(directory, entry);
    const zipPath = `${zipPrefix}/${entry}`;
    const stat = fs.lstatSync(source);
    if (stat.isDirectory() && !stat.isSymbolicLink()) {
      addDirectoryToZip(zipFile, source, zipPath);
    } else {
      zipFile.addLocalFile(source, path.posix.dirname(zipPath));
    }
  }
}

function requiredReleaseFiles(platform) {
  if (platform === 'win32') {
    return [
      'libEGL.dll',
      'libGLESv2.dll',
      ['libEGL.lib', 'libEGL.dll.lib'],
      ['libGLESv2.lib', 'libGLESv2.dll.lib']
    ];
  }
  if (platform === 'darwin') {
    return ['libEGL.dylib', 'libGLESv2.dylib'];
  }
  if (platform === 'linux') {
    return ['libEGL.so', 'libGLESv2.so'];
  }
  throw new Error(`Unsupported platform: ${platform}`);
}

function resolveRequiredFile(releasePath, fileSpec) {
  if (Array.isArray(fileSpec)) {
    for (const fileName of fileSpec) {
      const filePath = path.join(releasePath, fileName);
      if (fs.existsSync(filePath)) {
        return fileName;
      }
    }
    throw new Error(`Missing one of: ${fileSpec.join(', ')}`);
  }

  const filePath = path.join(releasePath, fileSpec);
  if (!fs.existsSync(filePath)) {
    throw new Error(`Missing required ANGLE output: ${filePath}`);
  }
  return fileSpec;
}

function stageAngleTree(sourcePath, stagingPath, platform) {
  const releasePath = path.join(sourcePath, 'out', 'Release');
  const includePath = path.join(sourcePath, 'include');
  if (!fs.existsSync(includePath)) {
    throw new Error(`Missing ANGLE include directory: ${includePath}`);
  }
  const requiredHeaders = [
    'EGL/egl.h',
    'EGL/eglext.h',
    'GLES2/gl2.h',
    'GLES2/gl2ext.h',
    'GLES3/gl3.h',
    'GLES3/gl32.h'
  ];
  for (const header of requiredHeaders) {
    const headerPath = path.join(includePath, header);
    if (!fs.existsSync(headerPath)) {
      throw new Error(`Missing required ANGLE header: ${headerPath}`);
    }
  }

  const stagedAnglePath = path.join(stagingPath, 'angle');
  const stagedReleasePath = path.join(stagedAnglePath, 'out', 'Release');
  fs.mkdirSync(stagedReleasePath, {recursive: true});

  for (const fileSpec of requiredReleaseFiles(platform)) {
    const fileName = resolveRequiredFile(releasePath, fileSpec);
    copyRecursive(
        path.join(releasePath, fileName),
        path.join(stagedReleasePath, fileName));
  }
  copyRecursive(includePath, path.join(stagedAnglePath, 'include'));
}

function createArchive(stagingPath, outputPath, platform) {
  if (platform === 'win32') {
    const zipFile = new AdmZip();
    addDirectoryToZip(zipFile, path.join(stagingPath, 'angle'), 'angle');
    zipFile.writeZip(outputPath);
    return;
  }

  cp.execFileSync('tar', ['-czf', outputPath, '-C', stagingPath, 'angle'], {
    stdio: 'inherit'
  });
}

function main() {
  const args = parseArgs(process.argv);
  const sourcePath = path.resolve(args.source || '');
  const version = args.version;
  const outputDir = path.resolve(args['out-dir'] || '');
  if (!sourcePath || !version || !outputDir) {
    usage();
    process.exit(1);
  }

  const platform = os.platform();
  const arch = os.arch().toLowerCase();
  const extension = platform === 'win32' ? 'zip' : 'tar.gz';
  const archiveName = `angle-${version}-${platform}-${arch}.${extension}`;
  const outputPath = path.join(outputDir, archiveName);
  const stagingPath = path.join(
      outputDir, `_tmp-angle-package-${process.pid}-${platform}-${arch}`);

  fs.rmSync(stagingPath, {recursive: true, force: true});
  fs.mkdirSync(stagingPath, {recursive: true});
  fs.mkdirSync(outputDir, {recursive: true});

  try {
    stageAngleTree(sourcePath, stagingPath, platform);
    fs.rmSync(outputPath, {force: true});
    createArchive(stagingPath, outputPath, platform);
  } finally {
    fs.rmSync(stagingPath, {recursive: true, force: true});
  }

  console.log(outputPath);
}

main();
