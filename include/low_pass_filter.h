#pragma once

/**
 * @brief Simple exponential low-pass filter for sensor smoothing.
 *
 * Reduces noise and spurious values using an exponential moving average.
 * Formula: filtered = (alpha * raw) + ((1 - alpha) * lastFiltered)
 *
 * alpha range:
 *   - 0.0 = no change (infinite smoothing)
 *   - 1.0 = immediate response (no smoothing)
 *   - 0.1-0.2 = typical for sensor noise (equivalent to 5-10 sample average)
 */
class LowPassFilter
{
public:
    /**
     * @brief Initialize the filter.
     *
     * @param initialValue Starting output value (typically first sensor reading).
     * @param filterAlpha  Smoothing coefficient (0.0 to 1.0). Default 0.15.
     */
    LowPassFilter(float initialValue = 0.0F, float filterAlpha = 0.15F)
        : m_lastFiltered(initialValue), m_alpha(filterAlpha)
    {
    }

    /**
     * @brief Process a new raw sensor value.
     *
     * @param newValue Raw sensor reading.
     * @return Filtered output value.
     */
    float update(float newValue)
    {
        m_lastFiltered = (m_alpha * newValue) + ((1.0F - m_alpha) * m_lastFiltered);
        return m_lastFiltered;
    }

    /**
     * @brief Get the current filtered value without updating.
     *
     * @return Last computed filtered value.
     */
    float get() const
    {
        return m_lastFiltered;
    }

    /**
     * @brief Reset filter state to a new initial value.
     *
     * @param initialValue New starting value.
     */
    void reset(float initialValue)
    {
        m_lastFiltered = initialValue;
    }

    /**
     * @brief Change the smoothing coefficient.
     *
     * @param newAlpha New alpha value (0.0 to 1.0).
     */
    void setAlpha(float newAlpha)
    {
        m_alpha = newAlpha;
    }

private:
    float m_lastFiltered;  ///< Last filtered output value.
    float m_alpha;         ///< Smoothing coefficient (0.0 to 1.0).
};
