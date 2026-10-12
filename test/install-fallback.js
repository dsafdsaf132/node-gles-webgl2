const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const scriptPath = path.join(__dirname, '..', 'scripts', 'install.js');
const source = fs.readFileSync(scriptPath, 'utf8');

for (const [node, expected] of [
  ['12.22.12', '8.4.1'], ['16.13.2', '8.4.1'], ['16.14.0', '10.3.1'],
  ['18.20.8', '10.3.1'], ['20.16.0', '10.3.1'], ['20.17.0', '12.3.0'],
  ['21.7.3', '10.3.1'], ['22.8.0', '10.3.1'], ['22.9.0', '12.3.0'],
  ['23.0.0', '12.3.0'], ['24.15.0', '12.3.0']
]) {
  for (const failSystemGyp of [false, true]) {
    const commands = [];
    const mocks = {
      child_process: {
        execSync(command) {
          commands.push(command);
          if (failSystemGyp && commands.length === 1) throw new Error('missing');
        }
      },
      fs: {existsSync: () => true, mkdirSync() {}, copyFileSync() {}},
      os: {platform: () => 'win32', arch: () => 'x64'},
      path,
      './resolve-angle-path': () => 'mock-angle'
    };
    vm.runInNewContext(source, {
      require(name) {
        assert(Object.prototype.hasOwnProperty.call(mocks, name), name);
        return mocks[name];
      },
      __dirname: path.dirname(scriptPath),
      process: {versions: {node}},
      console: {error() {}}
    }, {filename: scriptPath});
    assert.deepStrictEqual(commands, failSystemGyp ?
      ['node-gyp rebuild', `npx --yes node-gyp@${expected} rebuild`] :
      ['node-gyp rebuild'], node);
  }
}
console.log('Install fallback version boundaries passed');
