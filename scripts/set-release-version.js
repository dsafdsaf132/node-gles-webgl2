/**
 * @license
 * Copyright 2026 node-gles-webgl2 contributors.
 * Licensed under the Apache License, Version 2.0.
 */

const fs = require('fs');
const path = require('path');

const version = process.argv[2];
if (!version) {
  throw new Error('Usage: node scripts/set-release-version.js <version>');
}

const rootPath = path.join(__dirname, '..');
const packagesPath = path.join(rootPath, 'packages');
const manifests = [
  path.join(rootPath, 'package.json'),
  ...fs.readdirSync(packagesPath)
      .map(directory => path.join(packagesPath, directory, 'package.json'))
      .filter(file => fs.existsSync(file))
];

for (const manifestPath of manifests) {
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  manifest.version = version;
  if (manifest.optionalDependencies) {
    for (const dependency of Object.keys(manifest.optionalDependencies)) {
      if (dependency.startsWith('@dsafdsaf132/node-gles-webgl2-')) {
        manifest.optionalDependencies[dependency] = version;
      }
    }
  }
  fs.writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
}
