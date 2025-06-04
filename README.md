# Tangram NG #
*Fork of [Tangram ES](https://github.com/tangrams/tangram-es) for [Ascend Maps](https://www.github.com/styluslabs/maps)*

Cross-platform C++ library for rendering 2D and 3D maps, supporting vector (GeoJSON, TopoJSON, Mapbox Vector Tiles) and raster (PNG, JPEG, TIFF, and LERC) data.

Map style is defined by YAML scene files - see Tangram ES documentation at https://tangrams.readthedocs.io

<img alt="Screenshot" src="https://github.com/user-attachments/assets/206324b9-bec1-46ca-a9e6-9f7fc1ec6d00" width="810">

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
* replace old yaml-cpp with custom yaml/json library (to fix crashes)


## Demo ##

[Ascend Maps](https://www.github.com/styluslabs/maps) uses a cross-plaform GUI framework ([ugui](https://www.github.com/styluslabs/ugui)) instead of native GUI.  For examples of how to integrate tangram-ng with more conventional native apps, see the demos in the [platforms](platforms/) folder (note that the platform-specific documentation has not been updated yet).  The scene file [osm-bright.yaml](res/osm-bright.yaml) was converted from Mapbox style JSON using [mb2mz.js](https://github.com/styluslabs/maps/blob/master/scripts/runmb2mz.js).

To build the demos (requires cmake):
* Linux: `make linux` to create `build/Release/tangram`
* Android: `cd platforms/android && ./gradlew installRelease` (update ndkVersion in tangram/build.gradle as needed)
* iOS, macOS, Windows: demos not yet updated from tangram-es versions, please open a github issue if needed


## More ##

Contains a couple potentially useful libraries:
* [gaml](core/deps/gaml/src) - simple (~1000 LOC, 2 files) YAML parser and writer, supporting both block and flow style.  Can preserve comments and order of map keys
* [sqlitepp](core/deps/sqlite3/sqlitepp.h) - single header (~200 LOC) wrapper for sqlite supporting expressions like `db.stmt("select a,b,c from some_table where d = ? and e = ?;").bind(5, "foo").exec([&](int a, float b, std::string c) { /* called for each result row */ });`


## Contributing ##

Contributions are welcome, but please open an issue or discussion before starting on major changes.
