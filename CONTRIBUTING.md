# Contributing

Contributions are welcome through GitHub issues and pull requests.

## Development

Install dependencies without running the native install script:

```sh
npm install --ignore-scripts
```

Build the JavaScript package files:

```sh
npm run build
```

Build the ANGLE-backed native addon:

```sh
node scripts/install.js
```

On Ubuntu, install the native build dependencies first:

```sh
sudo apt-get install -y build-essential python3 libx11-dev
```

## Pull Requests

- Keep changes focused and avoid unrelated refactors.
- Update documentation when behavior or install steps change.
- Run the relevant build checks before opening a pull request.

By contributing, you agree that your contribution is licensed under the
Apache-2.0 license used by this project.
