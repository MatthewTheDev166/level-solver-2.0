#include <Geode/Geode.hpp>
#include "ui/SolverOverlay.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    SolverOverlay::setup();
}

