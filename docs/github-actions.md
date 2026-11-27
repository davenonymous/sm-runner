# GitHub Actions

Two actions run smrunner in the CI of a plugin's own repository: `setup` installs smrunner, `test`
compiles the plugins and runs their tests.

- [Quick start](#quick-start)
- [Versions](#versions)
- [The setup action](#the-setup-action)
- [The test action](#the-test-action)
- [Results](#results)
- [Examples](#examples)

## Quick start

```yaml
name: Tests
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4
      - uses: davenonymous/sm-runner/setup@v0
        with:
          spcomp: true
      - uses: davenonymous/sm-runner/test@v0
        with:
          tests: tests/test_*.sp
          plugins: scripting/greeter.sp
```

`setup` installs smrunner and SourceMod's compiler. `test` compiles `greeter.sp` and the test
plugins, runs the tests with `smrunner` and `smrunner64`, and fails the step if a test fails. The
results show up as a job summary and as annotations on the lines of the failed assertions.

The actions run on Linux runners only, with `bash` and `python3` (both are on GitHub's Ubuntu
runners).

## Versions

Use the actions at a release tag:

| Reference | Installs |
|---|---|
| `@v0.1.0` | smrunner 0.1.0. |
| `@v0` | The newest 0.x.y release. The tag moves with each release; prereleases leave it alone. |
| `@main`, a branch or a commit | Nothing by itself: set `version:`. |

The actions take the version from the tag they are used at. `version: 0.1.0` installs another
release. The download is checked against the `.sha256` file published with each release.

## The setup action

`davenonymous/sm-runner/setup` installs smrunner below `$RUNNER_TEMP` and puts its `bin/` on
`PATH`, so later steps can run `smrunner` and `smrunner64` themselves.

| Input | Default | Effect |
|---|---|---|
| `version` | the action's tag | The release to install, for example `0.1.0`. |
| `archive` | | A local `smrunner-*-linux.tar.gz` to install instead of a release, for example an unreleased build from smrunner's CI. Excludes `version`. |
| `spcomp` | `false` | Also install `spcomp64` and SourceMod's include files, from SourceMod 1.12.0.7263's release package. |
| `sourcemod-url` | | Another SourceMod Linux package to take `spcomp64` and the include files from, for example a 1.13 build. Needs `spcomp: true`. |
| `sourcemod-sha256` | | The SHA-256 of `sourcemod-url`. Without it the package is not checked, and a warning says so. |
| `install-32bit-runtime` | `true` | Install `libc6-i386` and `lib32stdc++6` with `apt-get`, which the 32-bit `smrunner` needs. Set `false` on runners without `sudo`, or when you only use `smrunner64`. |
| `cache` | `false` | Keep the installation in the Actions cache between workflow runs. Not with `archive`. |

It sets these environment variables for the later steps:

| Variable | Value |
|---|---|
| `SMRUNNER_HOME` | smrunner's install folder. |
| `SMRUNNER_INCLUDE` | The folder of `smrunner.inc` and `smrunner-steamworks.inc`. |
| `SPCOMP` | The path of `spcomp64` (with `spcomp: true`). Its folder is also on `PATH`. |
| `SOURCEMOD_INCLUDE` | SourceMod's include folder (with `spcomp: true`). |

## The test action

`davenonymous/sm-runner/test` runs `smrunner test`. Every path and glob is relative to
`working-directory`. Inputs that take a list take one entry per line.

| Input | Default | Effect |
|---|---|---|
| `tests` | (required) | The test plugins, `.sp` or `.smx`, paths or globs (`**` works). A glob that matches nothing gives a warning; no test plugin at all is an error. |
| `plugins` | | The plugins under test, `.sp` or `.smx`, paths or globs, loaded in this order. A line that matches no file goes to `--plugin` as it is, so `adminmenu` loads SourceMod's stock plugin. |
| `runner` | `both` | `smrunner`, `smrunner64` or `both`. With `both`, both runners always run, and the step fails if either fails. |
| `working-directory` | `.` | The folder the paths are relative to. |
| `include-paths` | | Further include folders for compiling. |
| `extensions` | | Folders for `--extensions`. |
| `extension` | | Files for `--extension`. |
| `cvars` | | `NAME=VALUE` for `--cvar`. |
| `files` | | `SRC=DEST` for `--file`. |
| `core` | | `KEY=VALUE` for `--core`. |
| `filter` | | `--filter TEXT`. |
| `timeout` | | `--timeout SECONDS`. |
| `steamworks` | | `true` for `--steamworks`; a folder for `--steamworks=DIR`. |
| `steamworks-passthrough` | | Hosts for `--steamworks-passthrough`. Needs `steamworks`. |
| `args` | | Any other arguments of `smrunner test`, one argument per line. |
| `annotations` | `true` | An error annotation for each failure. |
| `upload` | `false` | Upload the results as an artifact. |
| `artifact-name` | `smrunner-results-<job>-<random>` | The artifact's name. |
| `version` | the action's tag | The release to install if no `setup` step ran before. |

The options are described in [Command line](command-line.md).

**Compiling.** Each `.sp` file of `tests` and `plugins` is compiled on its own with `spcomp64`,
into a folder below `$RUNNER_TEMP`. The include folders are, in this order: `include-paths`, an
`include/` folder beside the `.sp` file if there is one, smrunner's includes and SourceMod's. If a
file does not compile, the compiler's output is printed and the step fails. Compiling needs
`spcomp: true` on the `setup` step; without it, the step fails and says so.

**Without a setup step.** If no `setup` step ran before in the job, `test` installs smrunner
itself, with `spcomp64` if it has `.sp` files and with the 32-bit runtime if it uses `smrunner`.
That installation is not cached; use a `setup` step with `cache: true` for that.

## Results

- **Job summary.** The totals of each runner, and a table of every test that did not pass, with
  its runner, status and messages.
- **Annotations.** Each failure is an error annotation, titled with the runner and the test.
  smrunner names a failed assertion's place by file name (`(test_greeter.sp:13)`); the action
  looks that name up among the repository's files below `working-directory` and puts the
  annotation on that line if exactly one file has that name. A timeout, a crash or a failure
  without a place gets an annotation without a file. `annotations: false` turns them off.
- **Artifact.** With `upload: true`, the artifact holds `junit-<runner>.xml`, the printed report
  `report-<runner>.txt` of each runner, and the compiled plugins in `smx/`. It is uploaded also
  when tests fail. A composite action cannot see the `matrix` context, so in a matrix job set
  `artifact-name` to something readable, for example `results-${{ matrix.target }}`.
- **Step result.** The step fails when a test fails, times out or crashes, when no test matches
  `filter`, or when smrunner cannot run. Use `continue-on-error: true` to go on and read the
  outputs.

| Output | Value |
|---|---|
| `result` | `success` if every runner passed, otherwise `failure`. |
| `tests`, `passed`, `failed`, `skipped` | The counts, summed over the runners. `failed` includes timed-out and crashed tests. |
| `junit` | The JUnit XML files, one path per line. |
| `artifact-name` | The name of the uploaded artifact; empty without `upload`. |

## Examples

**A plugin with an extension and its own includes**, only on 64-bit servers, with cached setup:

```yaml
      - uses: davenonymous/sm-runner/setup@v0
        with:
          spcomp: true
          cache: true
          install-32bit-runtime: false
      - uses: davenonymous/sm-runner/test@v0
        with:
          runner: smrunner64
          tests: tests/*.sp
          plugins: scripting/json-status.sp
          include-paths: |
            scripting/include
            deps/smjansson/scripting/include
          extensions: deps/smjansson/extensions
```

**Precompiled plugins and a stock plugin**, without a setup step:

```yaml
      - uses: davenonymous/sm-runner/test@v0
        with:
          tests: build/test_*.smx
          plugins: |
            build/team-admin.smx
            adminmenu
```

**Several test suites in a matrix**, with readable artifact names:

```yaml
    strategy:
      matrix:
        suite: [readyup, votes]
    steps:
      - uses: actions/checkout@v4
      - uses: davenonymous/sm-runner/setup@v0
        with:
          spcomp: true
      - id: tests
        uses: davenonymous/sm-runner/test@v0
        with:
          tests: tests/${{ matrix.suite }}/*.sp
          plugins: scripting/${{ matrix.suite }}.sp
          upload: true
          artifact-name: smrunner-${{ matrix.suite }}
```

**Running smrunner directly** after `setup`, for run mode:

```yaml
      - uses: davenonymous/sm-runner/setup@v0
      - run: smrunner run build/greeter.smx --command "sm_greet" --frames 10
```
