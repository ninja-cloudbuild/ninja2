# Ninja2

Ninja2 is the second generation of Ninja that supports CloudBuild(Distributed Build System) and Ninja2 is fully compatible with the Ninja.

CloudBuild refer to https://gitee.com/cloudbuild888/cloudbuild

## QuickStart

Install the latest prebuild release version

```
wget -c https://raw.githubusercontent.com/ninja-cloudbuild/ninja2/refs/heads/main/install.sh && chmod +x install.sh && sudo ./install.sh
```

Enable CloudBuild as default in /etc/ninja2.conf

```
cloudbuild: true
grpc_url: "grpc://CLOUDBUILD_SERVICE_IP:PORT"
```

Enable CloudBuild as once

```
$ ninja --cloudbuild grpc://CLOUDBUILD_SERVICE_IP:PORT [-r PROJECT_ROOT_DIR]
```
- `--cloudbuild` can be used instead of `-c`
- When the working directory is not the root directory, you should use `-r`, which can replace `--project-root-dir`, 

### Build & install from source code

```
git clone https://github.com/ninja-cloudbuild/ninja2.git
cd ninja2
```
1. build: `./build.sh build`

2. install: `./build.sh install`

3. package: `./build.sh package`

4. clean: `./build.sh clean` or `rm -rf ./build`

### Developing in vscode-devcontainer

1. environment prepare: https://code.visualstudio.com/docs/devcontainers/containers

2. developing inside a Container: Press F1 in VS Code and execute the command "DevContainers: Reopen in Container".

# Ninja

Ninja is a small build system with a focus on speed.
https://ninja-build.org/

See [the manual](https://ninja-build.org/manual.html) or
`doc/manual.asciidoc` included in the distribution for background
and more details.

Binaries for Linux, Mac and Windows are available on
  [GitHub](https://github.com/ninja-build/ninja/releases).
Run `./ninja -h` for Ninja help.

Installation is not necessary because the only required file is the
resulting ninja binary. However, to enable features like Bash
completion and Emacs and Vim editing modes, some files in misc/ must be
copied to appropriate locations.

If you're interested in making changes to Ninja, read
[CONTRIBUTING.md](CONTRIBUTING.md) first.

## Building Ninja itself

You can either build Ninja via the custom generator script written in Python or
via CMake. For more details see
[the wiki](https://github.com/ninja-build/ninja/wiki).

### Python

```
./configure.py --bootstrap
```

This will generate the `ninja` binary and a `build.ninja` file you can now use
to build Ninja with itself.

### CMake

```
cmake -Bbuild-cmake
cmake --build build-cmake
```

The `ninja` binary will now be inside the `build-cmake` directory (you can
choose any other name you like).

To run the unit tests:

```
./build-cmake/ninja_test
```

## Generating documentation

### Ninja Manual

You must have `asciidoc` and `xsltproc` in your PATH, then do:

```
./configure.py
ninja manual doc/manual.pdf
```

Which will generate `doc/manual.html`.

To generate the PDF version of the manual, you must have `dblatext` in your PATH then do:

```
./configure.py    # only if you didn't do it previously.
ninja doc/manual.pdf
```

Which will generate `doc/manual.pdf`.

### Doxygen documentation

If you have `doxygen` installed, you can build documentation extracted from C++
declarations and comments to help you navigate the code. Note that Ninja is a standalone
executable, not a library, so there is no public API, all details exposed here are
internal.

```
./configure.py   # if needed
ninja doxygen
```

Then open `doc/doxygen/html/index.html` in a browser to look at it.

# License

* [Ninja2](https://github.com/ninja-cloudbuild/ninja2) and [CloudBuild](https://gitee.com/cloudbuild888/cloudbuild) are released under a dual license of AGPL-3.0 and a paid commercial license.
* [Ninja](https://github.com/ninja-build/ninja) is released under Apache License 2.0.
