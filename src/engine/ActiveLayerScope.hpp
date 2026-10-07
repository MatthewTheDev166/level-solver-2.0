#pragma once
#include <Geode/Geode.hpp>

class ActiveLayerScope {
public:
    explicit ActiveLayerScope(PlayLayer* layer) : m_layer(layer) {}
    ~ActiveLayerScope() = default;

    PlayLayer* getLayer() const { return m_layer; }

private:
    PlayLayer* m_layer = nullptr;
};
