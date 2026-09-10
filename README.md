> **This branch (`wayland-v2-3-stable`) carries an unofficial native Wayland
> patch set for Gazebo, built against OGRE-Next 2.3.** See below for what it
> is, upstream status, and how to build it. This section will be trimmed
> back down (or replaced with just a pointer) once/if the work upstream
> settles one way or another.

# Native Wayland support for Gazebo (unofficial patch set)

This is a working, verified-end-to-end set of patches across OGRE-Next and
three Gazebo libraries that lets `gz sim`'s GUI render natively under
Wayland instead of falling back to XWayland. It is **not merged upstream**
and, per current maintainer feedback, may not be for a while — see
"Upstream status" below. This document exists so you (or anyone else who
wants working Wayland support today) doesn't have to wait on that.

## What you get

- OGRE-Next's GL3Plus render system gains a native Wayland EGL windowing
  backend (`"Wayland EGL Window"` interface), selectable at runtime the
  same way `"Headless EGL / PBuffer"` already is.
- It adopts the GL context Qt already made current on the calling thread
  (mirroring how GLX's `currentGLContext`/`externalGLControl` params work),
  so Ogre and Qt share the same GL object namespace — this is what makes
  camera textures actually valid when Qt wraps them for display.
- `gz-rendering`'s `ogre2` backend passes a `wl_display`/`wl_surface` pair
  through to it.
- `gz-gui` obtains that Wayland display from Qt's public
  `QNativeInterface::QWaylandApplication` API and forces a desktop GL
  context (Qt's Wayland platform integration otherwise silently picks
  GLES, which the GL3Plus backend can't use at all).
- `gz-sim` gains an opt-in `GZ_GUI_WAYLAND=1` environment variable that
  skips the existing hardcoded XWayland fallback.

Verified: a real `gz sim` GUI session renders `examples/worlds/shapes.sdf`
correctly under native Wayland (cone, ellipsoid, sphere, box, cylinder,
capsule — correct colors/materials/lighting/shadows, screenshot-confirmed),
with zero crashes for the full session duration.

## Upstream status (as of 2026-09-10)

Four PRs were opened. Short version: OGRE-Next's maintainers will only
accept this against their `master` branch (a major version ahead, 4.0),
but `gz-rendering` is pinned to OGRE-Next 2.3 with no plans to move in the
short/medium term — their real long-term direction is a from-scratch
external renderer (Bevy-based), not an OGRE-Next upgrade. So the official
path forward runs through getting a 2.3-line backport accepted after the
master version merges, which is being pursued separately but isn't fast.
Full detail in the PR threads:
- OGRE-Next: https://github.com/OGRECave/ogre-next/pull/593
- gz-rendering: https://github.com/gazebosim/gz-rendering/pull/1325

**Practical consequence for this patch set**: use this branch
(`wayland-v2-3-stable`), not the branch targeting OGRE-Next master — the
master-targeted version won't link against current `gz-rendering` at all,
since it's still pinned to 2.3.

## Branches to use

| Repo | Fork | Branch | Notes |
|---|---|---|---|
| OGRE-Next | `AbdulkadirEroglu/ogre-next` | `wayland-v2-3-stable` (this branch) | Built against OGRE-Next 2.3, matches what `gz-rendering` actually expects today |
| gz-rendering | `AbdulkadirEroglu/gz-rendering` | `wayland-integration` | |
| gz-gui | `AbdulkadirEroglu/gz-gui` | `wayland-support` | |
| gz-sim | `AbdulkadirEroglu/gz-sim` | `wayland-support` | |

## Build order

All four need to share one install prefix so they find each other via
CMake's `find_package`. No `sudo` required anywhere.

```bash
PREFIX=/path/to/your/install-prefix

# 1. OGRE-Next (this branch)
cmake -S ogre-next -B ogre-next/build \
  -DCMAKE_INSTALL_PREFIX=$PREFIX \
  -DCMAKE_PREFIX_PATH=/path/to/freeimage-and-other-deps \
  -DOGRE_BUILD_RENDERSYSTEM_GL3PLUS=ON \
  -DOGRE_GLSUPPORT_USE_GLX=ON \
  -DOGRE_GLSUPPORT_USE_EGL_HEADLESS=ON \
  -DOGRE_GLSUPPORT_USE_EGL_WAYLAND=ON
cmake --build ogre-next/build -j$(nproc)
cmake --install ogre-next/build

# 2. gz-rendering, gz-gui, gz-sim, each in the usual gz-cmake way,
#    pointed at the same $PREFIX via CMAKE_PREFIX_PATH/CMAKE_INSTALL_PREFIX.
#    Standard gz build order applies: gz-cmake -> gz-utils -> gz-math ->
#    gz-common -> gz-plugin -> gz-rendering -> gz-transport/gz-msgs ->
#    gz-gui -> sdformat -> gz-physics -> gz-sensors -> gz-sim.
```

## Running it

```bash
unset QT_QPA_PLATFORM
GZ_GUI_WAYLAND=1 gz sim -v 4 examples/worlds/shapes.sdf
```

Without `GZ_GUI_WAYLAND=1`, behavior is unchanged (still forces XWayland).

## Known limitations

- No automated test coverage was added for the Wayland-specific code paths
  — the behavior needs a live Wayland compositor, which CI environments
  generally don't have. Verification throughout was manual: standalone
  harnesses plus real `gz sim` runs. See each repo's PR description for
  the exact manual reproduction steps used.
- Only tested against one compositor/driver combination (Hyprland +
  NVIDIA proprietary). Other compositors or Mesa drivers are untested.
- gz-physics has no physics backend built in the environment this was
  verified in — unrelated to Wayland, just noting that "no physics engine"
  errors in the log are a separate, pre-existing gap, not something this
  patch set causes.
- This is a personal fork, not a maintained package. If you build on top
  of it, expect to rebase periodically as the upstream repos move, and
  don't expect API stability guarantees.

## Why this exists / motivation

Xorg is effectively in maintenance-only mode across the Linux desktop
ecosystem, and Wayland is where things are headed. Gazebo is a useful
bellwether here — it's already several major versions ahead of what ROS
currently pins (ROS is still on an older Gazebo release). The goal of this
patch set is to make native Wayland support available now, both to use
directly and to give the upstream conversation something concrete to react
to, rather than waiting for that gap to close on its own timeline.

---

# OGRE3D (Object-Oriented Graphics Rendering Engine)

Ogre is a 3D graphics rendering engine. Not to be confused with a game engine which provides Networking, Sound, Physics, etc.

Ogre 2.3 has had a substantial overhaul to focus on high performance graphics using Data Oriented Design with:
 * Cache friendly Entity and Node layout
 * Threaded batch processing of Nodes, Frustum Culling and other techniques such as Forward Clustered
 * SIMD processing using AoSoA (Array of Structures of Arrays) memory layout
 * Texture loaded via background streaming

This makes Ogre suitable for projects aiming to have a **large number of objects on screen, or have tight rendering budgets such as VR.**

This is the repository where the 2.x branch is actively developed on.
Active development of the 1.x branch happens in https://github.com/OGRECave/ogre

Both branches are in active development. See [What version to choose?](https://www.ogre3d.org/about/what-version-to-choose) to understand the differences between 1.x and 2.x

Both repositories are compatible for merging, but have been split in separate ways as their
differences have diverged long enough.

| Build | Status (github) |
|-------|-----------------|
| MSVC | [![Build status](https://ci.appveyor.com/api/projects/status/github/OGRECave/ogre-next?branch=v2-3&svg=true)](https://ci.appveyor.com/project/MatiasNGoldberg/ogre-next/branch/v2-3)|

## Supported Backends

 * Direct3D 11
 * OpenGL 3.3+
 * Metal
 * Vulkan

## Supported Platforms

 * Windows (XP\*, 7, 8, 10)
 * Linux
 * macOS\*\*
 * iOS
 * Android\*\*\*

(\*) XP support is through GL3+. Recent drivers are needed. Old GPUs do not have stable GL drivers capable of running Ogre 2.x.<br/>
(\*\*) Metal Backend is highly recommended. GL backend is supported in macOS, but the window subsystem hasn't been ported to 2.3 yet.<br/>
(\*\*\*) Device must be Vulkan-capable. Android 7.0+ is supported; but Android 8.0+ is strongly recommended due to lots of driver bugs in older versions.<br/>

## Supported Compilers

 * Clang 3.3 or newer
 * GCC 5 or newer
 * VS2008 or newer
 
## Samples
For a list of samples and their demonstrated features, refer to the [samples section in the manual.](https://ogrecave.github.io/ogre-next/api/2.3/_samples.html) 

# Who's using it?

## [Yoy Simulators](https://www.yoy.cl/)

![](./Docs/frontpage/YoySimulators.jpg)

## [Skyline Game Engine](https://aurasoft-skyline.co.uk/)

![](./Docs/frontpage/SkylineGameEngineEditorFull.jpg)

## [Racecraft](https://store.steampowered.com/app/346610/Racecraft/)

![](./Docs/frontpage/Racecraft.jpg)

## [Sunset Rangers](https://store.steampowered.com/app/559340/Sunset_Rangers/)

![](./Docs/frontpage/SunsetRangers.jpg)


# Features

## Forward Clustered

![](./Docs/frontpage/ForwardClustered.jpg)

## PBS & HDR

![](./Docs/frontpage/HDR.jpg)

## Area Lights

![](./Docs/frontpage/AreaLights.jpg)

## Voxel Cone Tracing (VCT) GI

![](./Docs/frontpage/VCT.jpg)

## Instant Radiosity GI

![](./Docs/frontpage/InstantRadiosity.jpg)

## [Voxel Cone Tracing + Per Pixel Parallax Corrected Cubemap (PCC) Hybrid](https://www.ogre3d.org/2019/08/14/pcc-vct-hybrid-progress)

![](./Docs/frontpage/VctPccHybrid.jpg)

## [OpenVR Integration](https://www.ogre3d.org/2019/09/22/improvements-in-vr-morph-animations-moving-to-github-and-ci)

![](./Docs/frontpage/OpenVR.jpg)

# Dependencies

* [CMake 3.x](https://cmake.org/download/)
* Git
* For HW & SW requirements, please visit http://www.ogre3d.org/developers/requirements
* Our source dependencies are grouped in [ogre-next-deps](https://github.com/OGRECave/ogre-next-deps) repo
* Python 3.x is needed to build shaderc dependency for Vulkan.

# Dependencies (Windows)

* Visual Studio 2008 SP1 - 2017 (2019 not tested). MinGW may work but we strongly recommend Visual Studio.
* [DirectX June 2010 SDK](https://www.microsoft.com/en-us/download/details.aspx?id=6812). Optional.
  Needed if you use older Visual Studio versions and want the D3D11 plugin. Also comes with useful tools.
* Windows 10 SDK. Contains the latest DirectX SDK, thus recommended over the DX June 2010 SDK,
  but you may still want to install the June 2010 SDK for those tools.
* Windows 7 or newer is highly recommended. For Windows Vista & 7, you need to have the
  [KB2670838 update](https://support.microsoft.com/en-us/kb/2670838) installed.
  **YOUR END USERS NEED THIS UPDATE AS WELL**.

# Dependencies (Linux)

* Clang >3.5 or GCC >4.0

Debian-based. Run:

```
sudo apt-get install libfreetype6-dev libfreeimage-dev libzzip-dev libxrandr-dev libxaw7-dev freeglut3-dev libgl1-mesa-dev libglu1-mesa-dev doxygen graphviz python-clang-4.0 libsdl2-dev cmake ninja-build git
```

Arch-based Run:

```
pacman -S freeimage freetype2 libxaw libxrandr mesa zziplib cmake gcc
```

# Quick Start

We provide quick download-build scripts under the [Scripts/BuildScripts/output](Scripts/BuildScripts/output) folder.

You can download all of these scripts [as a compressed 7zip file](https://bintray.com/darksylinc/ogre-next/download_file?file_path=build_ogre_scripts-master.7z)

If you're on Linux, make sure to first install the dependencies (i.e. run the sudo apt-get above)

# Download and Building manually

If for some reason you want to do it by hand, there's no script for your platform,
or you want to learn what the scripts are actually doing, see
[Setting Up Ogre](https://ogrecave.github.io/ogre-next/api/2.3/_setting_up_ogre.html) from the Ogre manual.

# Manual

For more information see the [online manual](https://ogrecave.github.io/ogre-next/api/2.3/manual.html).
The manual can build on Linux using Doxygen:

```
cd build/Debug
ninja OgreDoc
```

# Support and Resources

 * [Forums](https://forums.ogre3d.org/viewforum.php?f=25)
 * [Bug Reports](https://github.com/OGRECave/ogre-next/issues)
 * [Contributing via Pull Requests](https://github.com/OGRECave/ogre-next/pulls)
 * [Documentation](https://ogrecave.github.io/ogre-next/api/2.3/)
 * [Ogre 2.1+ FAQ](http://wiki.ogre3d.org/Ogre+2.1+FAQ)
 * [Older resources for interfaces carried over from 1.x](https://www.ogre3d.org/documentation)

# Samples

If you want to test or evaluate Ogre, you can try the [prebuilt samples for Windows](https://bintray.com/darksylinc/ogre-next/download_file?file_path=ogre-samples-windows-x64-vs2015.7z).

# Unit Tests

To run the unit tests, go to Scripts/UnitTesting and to generate the comparison files type:

```
python3 RunUnitTests.py gl ../../build/Debug/bin/ ./JSON ../../build/UnitTestsOutput/
```

to check the diff against already generated data:

```
python3 RunUnitTests.py gl ../../build/Debug/bin/ ./JSON ../../build/UnitTestsOutput/ ../../build/UnitTestsOutput_old/
```

# License

OGRE (www.ogre3d.org) is made available under the MIT License.

Copyright (c) 2000-present Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
