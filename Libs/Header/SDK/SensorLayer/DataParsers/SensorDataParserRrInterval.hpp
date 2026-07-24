/**
 ******************************************************************************
 * @file    SensorDataParserRrInterval.hpp
 * @brief   SensorData parser for the RR_INTERVAL (beat-to-beat) sensor.
 ******************************************************************************
 *
 * EXPERIMENTAL — there is no firmware producer yet. This lands the SDK-side
 * half of an RR pathway so the HRV / DFA alpha1 pipeline can be built and
 * validated ahead of the firmware side; a companion simulator replay mock lands
 * separately. The wire shape may still change; see the open questions in the
 * PR/issue before treating 0x44 as stable.
 *
 * Opt-in companion to HEART_RATE / HEART_RATE_EX. Carries a single beat-to-beat
 * R-R interval — the gap between two consecutive heartbeats — which is what HRV
 * metrics are built from (RMSSD, SDNN, DFA alpha1, threshold detection).
 *
 * ONE INTERVAL PER FRAME. The underlying Data model has no in-band length field
 * (field count is derived from the delivery stride, fixed per driver), so a
 * frame cannot honestly carry a variable-length burst. The several R-R values in
 * one BLE 0x2A37 notification are therefore delivered as consecutive
 * one-interval frames — as separate deliveries under an event-based producer, or
 * batched if a producer uses queue capacity > 1. A consumer iterates
 * DataBatch::size() so it is correct either way.
 *
 * Field layout (float), lenient upper bound so a producer may append metadata
 * without an ABI break (mirrors HEART_RATE_EX). Fields are POSITIONAL and
 * appended in this exact order — a producer must never skip one (a 2-field frame
 * is [rr, source], never [rr, flags]):
 * - [0] rr_ms   (REQUIRED) the R-R interval in milliseconds
 * - [1] source  (optional) Source: 0 unknown, 1 optical, 2 external strap, 3 ECG
 * - [2] flags   (optional) bit0 discontinuity (first beat after a gap / not
 *                          contiguous with the previous interval), bit1 artifact-suspect,
 *                          bit2 no-skin-contact (strap's HR Service contact bit)
 *
 * Values arrive already converted from the BLE 1/1024 s wire unit to
 * milliseconds (float) producer-side; whether to instead carry the raw integer
 * tick is an open question. The frame timestamp is the beat instant the interval
 * ends on (a companion simulator replay mock, landing separately, reconstructs it
 * by spacing beats by their R-R value).
 * Whether a real BLE producer can supply a per-beat instant, or only the
 * notification-arrival time, is an open question for the firmware side, and it
 * matters beyond metrics: the per-listener sample-rate adapter gates delivery on
 * this timestamp, so a producer that stamps every R-R value in one 0x2A37
 * notification with a single arrival instant would have all but the first beat
 * dropped. A real producer must give each interval a distinct per-beat timestamp,
 * or the platform needs a true no-decimation event path for beat-level streams.
 *
 ******************************************************************************
 */

#ifndef __SENSOR_DATA_PARSER_RR_INTERVAL_HPP
#define __SENSOR_DATA_PARSER_RR_INTERVAL_HPP

#include "SDK/SensorLayer/SensorDataView.hpp"

#include <cstdint>

namespace SDK
{
    namespace SensorDataParser
    {
        /**
         * @brief Helper class for parsing one RR_INTERVAL frame from a DataView.
         */
        class RrInterval
        {
        public:
            /**
             * @brief Field indices (only RR_MS is guaranteed present).
             */
            enum Field : uint8_t {
                RR_MS  = 0, ///< R-R interval, milliseconds (required)
                SOURCE = 1, ///< producer of this interval (optional)
                FLAGS  = 2, ///< quality / continuity bits (optional)
            };

            /**
             * @brief Which producer emitted the interval.
             * @note  Values are intended to mirror SensorDataParser::HeartRateEx::Source,
             *        extended with ECG. The lock (a static_assert against the kernel
             *        arbiter enum, as HeartRateEx::Source has) lands with the producer;
             *        there is no such kernel enum to bind to yet, so the mapping is
             *        by-convention for now and nothing enforces it.
             */
            enum class Source : uint8_t {
                UNKNOWN  = 0, ///< none / unknown
                OPTICAL  = 1, ///< wrist optical (PPG)
                EXTERNAL = 2, ///< external BLE strap
                ECG      = 3, ///< electrocardiogram
            };

            /**
             * @brief Continuity / quality flag bits (FLAGS field).
             */
            enum Flag : uint32_t {
                DISCONTINUITY    = 1u << 0, ///< not contiguous with the previous interval (gap / reconnect / first)
                ARTIFACT_SUSPECT = 1u << 1, ///< producer flags this interval as likely artefactual
                NO_SKIN_CONTACT  = 1u << 2, ///< strap reports loss of skin contact (HR Service 0x2A37 contact bit)
            };

            /**
             * @brief Construct a new RrInterval parser over one frame.
             * @param data Sensor data view holding at least the RR_MS float.
             */
            RrInterval(const SDK::Sensor::DataView data) : mData(data) {}

            /**
             * @brief Whether the frame carries the required RR_MS field.
             * @note  Guards a future layout / malformed frame; with the current
             *        delivery path the field count is always >= 1.
             */
            bool isDataValid() const
            {
                return (mData.getFieldCount() >= 1);
            }

            /**
             * @brief The R-R interval in milliseconds (0 if invalid).
             */
            float getRrMs() const
            {
                return isDataValid() ? mData.f[Field::RR_MS] : 0.0f;
            }

            /**
             * @brief Implied instantaneous heart rate (bpm) for convenience.
             * @note  Instantaneous single-beat rate — NOT a smoothed HR; do not
             *        treat as a "max HR". 0 if the interval is invalid.
             */
            float getBpm() const
            {
                const float rr = getRrMs();
                return (rr > 0.0f) ? (60000.0f / rr) : 0.0f;
            }

            /**
             * @brief Producer of this interval, or UNKNOWN if not supplied.
             */
            Source getSource() const
            {
                if (mData.getFieldCount() <= Field::SOURCE) {
                    return Source::UNKNOWN;
                }
                switch (static_cast<uint8_t>(mData.f[Field::SOURCE])) {
                    case static_cast<uint8_t>(Source::OPTICAL):  return Source::OPTICAL;
                    case static_cast<uint8_t>(Source::EXTERNAL): return Source::EXTERNAL;
                    case static_cast<uint8_t>(Source::ECG):      return Source::ECG;
                    default:                                     return Source::UNKNOWN;
                }
            }

            /**
             * @brief True if this interval is NOT contiguous with the previous one
             *        (gap, reconnect, or first interval). When the producer does not
             *        supply flags, contiguity is assumed (returns false).
             */
            bool hasDiscontinuity() const
            {
                return flags() & Flag::DISCONTINUITY;
            }

            /**
             * @brief True if the producer marks this interval as artefact-suspect.
             */
            bool isArtifactSuspect() const
            {
                return flags() & Flag::ARTIFACT_SUSPECT;
            }

            /**
             * @brief True if the strap reports loss of skin contact for this reading
             *        (a data-quality signal for HRV). False when contact is good or
             *        the producer does not report it.
             */
            bool isSkinContactLost() const
            {
                return flags() & Flag::NO_SKIN_CONTACT;
            }

            uint32_t getTimestamp() const
            {
                return isDataValid() ? mData.getTimestamp() : 0;
            }

            uint64_t getTimestampUs() const
            {
                return isDataValid() ? mData.getTimestampUs() : 0;
            }

            /**
             * @brief Minimum fields a valid RR_INTERVAL frame carries (the RR value).
             */
            static constexpr uint8_t getFieldsNumber()
            {
                return 1;
            }

        private:
            // The FLAGS field is a bitmask carried in a float, matching the
            // HeartRateEx convention that all sensor fields are float. The bits
            // defined above are exact in float32; if flag bits ever exceed 23,
            // switch to the Field union's .u32 accessor to stay exact.
            uint32_t flags() const
            {
                return (mData.getFieldCount() > Field::FLAGS)
                           ? static_cast<uint32_t>(mData.f[Field::FLAGS])
                           : 0u;
            }

            const SDK::Sensor::DataView mData;
        }; /* class RrInterval */
    }; /* namespace SensorDataParser */

} /* namespace SDK */

#endif /* __SENSOR_DATA_PARSER_RR_INTERVAL_HPP */
