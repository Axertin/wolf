#pragma once

#include <chrono>

/**
 * @brief Simple frame timer utility for FPS and frame time measurements
 */
class FrameTimer
{
  private:
    std::chrono::high_resolution_clock::time_point lastFrameTime;
    double frameTimeMs = 0.0;
    double fps = 0.0;
    bool initialized = false;

  public:
    /**
     * @brief Update frame timer - call this once per frame
     */
    void update()
    {
        auto currentTime = std::chrono::high_resolution_clock::now();

        if (initialized)
        {
            auto deltaDuration = currentTime - lastFrameTime;
            frameTimeMs = std::chrono::duration<double, std::milli>(deltaDuration).count();
            fps = (frameTimeMs > 0.001) ? (1000.0 / frameTimeMs) : 0.0;
        }
        else
        {
            initialized = true;
            frameTimeMs = 0.0;
            fps = 0.0;
        }

        lastFrameTime = currentTime;
    }

    /**
     * @brief Get frame time in milliseconds
     * @return Frame time in milliseconds
     */
    double getFrameTimeMs() const
    {
        return frameTimeMs;
    }

    /**
     * @brief Get current frames per second
     * @return FPS value
     */
    double getFPS() const
    {
        return fps;
    }
};
