#include "fc/effects.h"

#include <algorithm>

namespace fc {

const char* toString(EffectKind kind) {
    return kind == EffectKind::CollectArea ? "collect_area" : "recolor_car";
}

bool parseEffectKind(const std::string& text, EffectKind& out) {
    if (text == "recolor_car") {
        out = EffectKind::RecolorCar;
        return true;
    }
    if (text == "collect_area") {
        out = EffectKind::CollectArea;
        return true;
    }
    return false;
}

}  // namespace fc
