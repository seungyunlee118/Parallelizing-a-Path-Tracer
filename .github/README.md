# Parallelizing a Path Tracer with std::thread

Programming Assignment 2, Phase 2, Parallel and Heterogeneous Computing (University of Houston, Fall 2026).

A Monte Carlo path tracer built on the course template ([stimlab/par-raytrace](https://codeberg.org/stimlab/par-raytrace)) and parallelized with `std::thread`:

- rendering runs in its own thread, so the image can be watched while it converges
- samples are accumulated in a float buffer (16 samples per pixel per pass by default)
- each pass is split across worker threads with block, cyclic or dynamic row scheduling

All changes are in `cpp/pathtrace.cpp`. The report is [report/PA2_Phase2_Report.pdf](../report/PA2_Phase2_Report.pdf).

## Requirements (Windows)

- Visual Studio 2022 or 2026 with the "Desktop development with C++" workload (MSVC, CMake and Ninja are included)
- Git
- [vcpkg](https://github.com/microsoft/vcpkg), with the `VCPKG_ROOT` environment variable set to its folder

## Get the code

```
git clone --recursive https://github.com/seungyunlee118/Parallelizing-a-Path-Tracer.git
cd Parallelizing-a-Path-Tracer
```

`tira` is a git submodule. If the repository was cloned without `--recursive`, run `git submodule update --init`.
GitHub's "Download ZIP" does not include submodules, so the build fails with it.

`cpp/pathtrace.cpp` already contains the implementation. Do not copy `cpp/pathtrace.cpp.template` over it (that step in the template README is only for a fresh template).

## Build

In "Developer PowerShell for VS":

```
cmake --preset default
cmake --build build
```

The first configure downloads the dependencies through vcpkg (glm, imgui, glfw, glad), which can take a few minutes.

## Run

```
.\build\raytrace.exe
```

The image gets less noisy with every pass. Moving the camera in the control window restarts the accumulation.

Settings are read from environment variables:

| Variable | Meaning | Default |
|---|---|---|
| `RT_THREADS` | number of worker threads | all hardware threads |
| `RT_SCHEDULE` | `block`, `cyclic` or `dynamic` | `dynamic` |
| `RT_SPP` | samples per pixel per pass | 16 |
| `RT_BENCH` | `1` runs the benchmark and prints CSV to the console | off |

Example in PowerShell:

```
$env:RT_THREADS = "8"; $env:RT_SCHEDULE = "cyclic"
.\build\raytrace.exe
Remove-Item Env:RT_THREADS, Env:RT_SCHEDULE
```

The benchmark tries every schedule with 1 up to the number of hardware threads, three times, and prints `BENCHMARK DONE` when it is finished (a few minutes).
