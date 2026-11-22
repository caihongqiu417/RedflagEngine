#pragma once

namespace Redflag {
    class GraphicsContext {
    public:
        virtual ~GraphicsContext() = default;
        virtual void Init() = 0;
        virtual bool BeginFrame() { return true; }
        virtual void SwapBuffers() = 0;
    };
}