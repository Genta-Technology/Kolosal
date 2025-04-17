#pragma once

#include "dx10_context.hpp"

#include <memory>

class GraphicContextFactory {
public:
    static std::unique_ptr<GraphicsContext> createDirectXContext()
    {
#ifdef _WIN32
        return std::make_unique<DX10Context>();
#else
        // Implement for other platforms if needed
        throw std::runtime_error("DirectX is only available on Windows");
#endif
    }
};
