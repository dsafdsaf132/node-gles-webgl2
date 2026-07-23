/**
 * @license
 * Copyright 2026 node-gles-webgl2 contributors.
 * Licensed under the Apache License, Version 2.0.
 */

const fs = require('fs');
const os = require('os');
const path = require('path');

const platform = os.platform().toLowerCase();
const arch = os.arch().toLowerCase();
const platformKey = `${platform}-${arch}`;
const packageName = `@dsafdsaf132/node-gles-webgl2-${platformKey}`;

function resolveAnglePath() {
  try {
    return path.dirname(require.resolve(`${packageName}/package.json`));
  } catch (err) {
    if (err.code !== 'MODULE_NOT_FOUND') {
      throw err;
    }
  }

  const repositoryPath =
      path.join(__dirname, '..', 'packages', `angle-${platformKey}`);
  if (fs.existsSync(path.join(repositoryPath, 'package.json'))) {
    return repositoryPath;
  }

  throw new Error(
      `The ANGLE package ${packageName} is not installed. ` +
      `Supported platforms are darwin, linux, and win32 on x64 or arm64.`);
}

if (require.main === module) {
  process.stdout.write(resolveAnglePath());
}

module.exports = resolveAnglePath;
