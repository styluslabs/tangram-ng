# Tangram NG #
*Fork of [Tangram ES](https://github.com/tangrams/tangram-es) for [Ascend Maps](https://www.github.com/styluslabs/maps)*

[Diff to upstream: compare/076b273..HEAD](https://github.com/styluslabs/tangram-ng/compare/076b273..HEAD)

Major changes include:
* 3D terrain support (incl. label occlusion, sky, etc.)
* eliminating duplicate tile loads (when a source is used multiple times)
* tile level-of-detail based on screen area (in pixels**2)
* try proxy tile if tile loading fails (for better offline support)
* TIFF and LERC raster support
* GLES 3 support
* support for native scene style functions (performance improvement over JS)
* support for MBTiles as cache for online source, incl. last access tracking and max-age
* optional fallback marker shown when a marker is hidden by collision
* support for zoom_offset < 0 (for better satellite imagery resolution when pixel scale > 1)
* contour line label support
* support JS function for generating tile URL (per tile)
* $latitude, $longitude in scene style for location dependent styling adjustments
* support SVG images embedded in scene style (with external SVG renderer, e.g., nanosvg)
* support for fixed boolean values in filters to allow use of scene globals
* canceling all URL requests when destroying Scene
* misc optimizations based on profiling
* support plain makefile build
* fix some proxy tile issues (e.g. cycles)
* fix some issues when very large number of labels present
* fix some crashes related to async Scene destruction

Dependency changes:
* make glm and stb submodules
* absorb tangrams/* submodules
* replace SQLiteCpp with simple single header (200LOC) sqlite C++ wrapper
* replace old yaml-cpp with custom yaml/json library (crashes due to non-atomic ref counting in tangrams/yaml-cpp)

<img alt="Screenshot" src="https://github.com/user-attachments/assets/266a3f36-6231-4f83-b4bb-e379cd54803c" width="810">


Tangram ES
==========

[![CircleCI](https://circleci.com/gh/tangrams/tangram-es.svg?style=shield)](https://app.circleci.com/pipelines/github/tangrams/tangram-es)
[![Windows CI](https://github.com/tangrams/tangram-es/actions/workflows/windows.yml/badge.svg)](https://github.com/tangrams/tangram-es/actions/workflows/windows.yml)
[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-v2.0%20adopted-ff69b4.svg)](CODE_OF_CONDUCT.md)

Tangram ES is a C++ library for rendering 2D and 3D maps from vector data using OpenGL ES. It is a counterpart to [Tangram](https://github.com/tangrams/tangram).

This repository contains both the core rendering library and sample applications that use the library on Android, iOS, macOS, Ubuntu, Windows, and Raspberry Pi.

![screenshot](images/screenshot.png)

## Platform Targets

For more information about building Tangram ES or using it in your project, see the individual platform pages below:

- [Android](platforms/android)
- [iOS](platforms/ios)
- [macOS](platforms/osx)
- [Ubuntu Linux](platforms/linux)
- [Raspberry Pi](platforms/rpi)
- [Windows](platforms/windows)

## Support

For concept overviews and technical reference, see the [Tangram Documentation](https://mapzen.com/documentation/tangram).

You can also find us in the tangram-chat gitter chat room: https://gitter.im/tangrams/tangram-chat

## Contributions Welcome

We gladly appreciate feedback, feature requests, and contributions. For information and instructions, see [CONTRIBUTING.md](CONTRIBUTING.md).

Please note that this project is released with a Contributor Code of Conduct. By participating in this project you agree to abide by its terms. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

Tangram ES was created by [Mapzen](https://www.mapzen.com/) and is now a [Linux Foundation Project](https://www.linuxfoundation.org/press-release/2019/01/mapzen-open-source-data-and-software-for-real-time-mapping-applications-to-become-a-linux-foundation-project/).
