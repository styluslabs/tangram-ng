#pragma once

#include "data/tileData.h"
#include "pbf/pbf.hpp"
#include "util/variant.h"

#include <memory>
#include <string>
#include <vector>

namespace Tangram {

class Tile;
class TileTask;
class MapProjection;

namespace Mvt {

    struct Geometry {
        std::vector<Point> coordinates;
        std::vector<int> sizes;
    };

    struct ParserContext {
        ParserContext(int32_t _sourceId) : sourceId(_sourceId){}

        int32_t sourceId;
        std::vector<std::string> keys;
        std::vector<Value> values;
        std::vector<protobuf::message> featureMsgs;
        Geometry geometry;
        // Map Key ID -> Tag values
        std::vector<int> featureTags;
        // Key IDs sorted by Property key ordering
        std::vector<int> orderedKeys;

        int tileExtent = 0;
        int winding = 0;
        // if not empty, getLayer() decodes only layers with these names (others are returned with name only)
        std::vector<std::string> layers;
    };

    // protobuf field numbers
    enum TileTag { LAYER = 3 };
    enum LayerTag { LAYER_NAME = 1, LAYER_FEATURE = 2, LAYER_KEY = 3, LAYER_VALUE = 4, LAYER_TILE_EXTENT = 5 };
    enum FeatureTag { FEATURE_ID = 1, FEATURE_TAGS = 2, FEATURE_TYPE = 3, FEATURE_GEOM = 4 };

    enum GeomCmd {
        moveTo = 1,
        lineTo = 2,
        closePath = 7
    };

    void getGeometry(ParserContext& _ctx, protobuf::message _geomIn);

    Feature getFeature(ParserContext& _ctx, protobuf::message _featureIn);

    Layer getLayer(ParserContext& _ctx, protobuf::message _layerIn);

    std::shared_ptr<TileData> parseTile(const TileTask& _task, int32_t _sourceId);

} // namespace Mvt

} // namespace Tangram
