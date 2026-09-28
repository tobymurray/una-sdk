
#pragma once

#include <cstdint>
#include <cstdbool>

namespace SDK::Interface {

/**
 * @brief   Vibro motor interface.
 */
class IVibro {
public:

    // Percentages are the DRV2625 library's nominal levels. On firmware 1.4.0 the steps
    // measure smaller: STRONG_CLICK_1..4 give about 100/95/86/77 % (re-check on newer firmware).
    enum Effect {
        NO_EFFECT                            = 0,  // silent
        STRONG_CLICK_100                     = 1,
        STRONG_CLICK_60                      = 2,
        STRONG_CLICK_30                      = 3,
        SHARP_CLICK_100                      = 4,
        SHARP_CLICK_60                       = 5,
        SHARP_CLICK_30                       = 6,
        SOFT_BUMP_100                        = 7,
        SOFT_BUMP_60                         = 8,
        SOFT_BUMP_30                         = 9,
        DOUBLE_CLICK_100                     = 10,
        DOUBLE_CLICK_60                      = 11,
        STRONG_BUZZ_100                      = 14,
        ALERT_750MS_100                      = 15,
        ALERT_1000MS_100                     = 16,
        STRONG_CLICK_1_100                   = 17,
        STRONG_CLICK_2_80                    = 18,
        STRONG_CLICK_3_60                    = 19,
        STRONG_CLICK_4_30                    = 20,
        MEDIUM_CLICK_1_100                   = 21,
        MEDIUM_CLICK_2_80                    = 22,
        MEDIUM_CLICK_3_60                    = 23,
        SHARP_TICK_1_100                     = 24,
        SHARP_TICK_2_80                      = 25,
        SHARP_TICK_3_60                      = 26,
        SHORT_DOUBLE_CLICK_STRONG_1_100      = 27,
        SHORT_DOUBLE_CLICK_STRONG_2_80       = 28,
        SHORT_DOUBLE_CLICK_STRONG_3_60       = 29,
        SHORT_DOUBLE_CLICK_STRONG_4_30       = 30,
        SHORT_DOUBLE_CLICK_MEDIUM_1_100      = 31,
        SHORT_DOUBLE_CLICK_MEDIUM_2_80       = 32,
        SHORT_DOUBLE_CLICK_MEDIUM_3_60       = 33,
        SHORT_DOUBLE_SHARP_TICK_1_100        = 34,
        SHORT_DOUBLE_SHARP_TICK_2_80         = 35,
        SHORT_DOUBLE_SHARP_TICK_3_60         = 36,
        LONG_DOUBLE_SHARP_CLICK_STRONG_1_100 = 37,
        LONG_DOUBLE_SHARP_CLICK_STRONG_2_80  = 38,
        LONG_DOUBLE_SHARP_CLICK_STRONG_3_60  = 39,
        LONG_DOUBLE_SHARP_CLICK_STRONG_4_30  = 40,
        LONG_DOUBLE_SHARP_CLICK_MEDIUM_1_100 = 41,
        LONG_DOUBLE_SHARP_CLICK_MEDIUM_2_80  = 42,
        LONG_DOUBLE_SHARP_CLICK_MEDIUM_3_60  = 43,
        LONG_DOUBLE_SHARP_TICK_1_100         = 44,
        LONG_DOUBLE_SHARP_TICK_2_80          = 45,
        LONG_DOUBLE_SHARP_TICK_3_60          = 46,
        BUZZ_1_100                           = 47,
        BUZZ_2_80                            = 48,
        BUZZ_3_60                            = 49,
        BUZZ_4_40                            = 50,
        BUZZ_5_20                            = 51,
        PULSING_STRONG_1_100                 = 52,
        PULSING_STRONG_2_60                  = 53,
        PULSING_MEDIUM_1_100                 = 54,
        PULSING_MEDIUM_2_60                  = 55,
        PULSING_SHARP_1_100                  = 56,
        PULSING_SHARP_2_60                   = 57,
    };

    // Maximum Notes includes pauses.
    static const uint8_t skMaxNotes = 8;

    struct Note {
        uint8_t  effect;    // 1 - 127,  0 - for pause
        uint8_t  loop;      // 1 - 3,    0 - no repeat (only for effect)
        uint32_t pause;     // 1 - 1270, 0 - for effect. In ms. Step 10 ms.
    };

    /**
     * @brief   Play effect.
     * @note    Turn on vibro before play.
     * @note    Commands are queued, so you can call this method without delay.
     * @param   effect: Effect to play;
     */
    virtual void play(uint8_t effect = Effect::STRONG_CLICK_100) = 0;

    /**
     * @brief   Play melody.
     * @note    Turn on vibro before play.
     * @param   melody: array of effects. Maximum 8 notes includes pauses.
     * @param   len: length of the array. (Max len is skMaxNotes)
     * @param   loop: main loop 1-6 times, 0 - no repeat, 7 - infinity loop.
     */
    virtual void play(const Note melody[], uint8_t len, uint8_t loop = 0) = 0;

    /**
     * @brief   Check whether the vibro is playing.
     * @retval  'true' if playing, 'false' otherwise.
     */
    virtual bool isPlaying() = 0;

    /**
     * @brief   Stop playing melody.
     */
    virtual void stop() = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IVibro() = default;

};

} // namespace SDK::Interface
