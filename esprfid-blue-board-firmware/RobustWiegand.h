#pragma once

#include <Arduino.h>

/*
Written by ChatGPT. Prompt:
> Write me an Arduino .h and .cpp file for handling Wiegand card readers. The code must be capable of handling flapping D0/D1 triggers, especially outside the 2ms spec.
*/

/*
 * RobustWiegand
 *
 * A small Wiegand D0/D1 receiver intended for noisy readers and wiring.
 *
 * Key behavior:
 *   - Uses FALLING-edge interrupts on D0 and D1.
 *   - Rejects edges that arrive too soon after the last accepted bit.
 *   - Does NOT require accepted bits to arrive exactly every 2 ms.
 *   - Uses a configurable idle gap to decide when a frame is complete.
 *   - Queues completed frames so a new card read does not overwrite the last one.
 *
 * Important limitation:
 *   Once a false edge is far enough from the previous accepted edge to look like
 *   a real Wiegand bit, timing alone cannot reliably distinguish it from a real
 *   bit. Use expected bit lengths and/or fix the electrical ringing in that case.
 */
class RobustWiegand {
public:
    static const uint8_t MAX_READERS = 4;
    static const uint8_t FRAME_QUEUE_SIZE = 4;

    struct Config {
        // Any D0/D1 falling edge closer than this to the previously accepted
        // Wiegand bit is treated as ringing / chatter and discarded.
        //
        // 1200 us works well for readers whose real bits are around 2000 us apart
        // while rejecting ~400-500 us duplicate edges.
        uint32_t minBitSpacingUs = 1200;

        // A frame is considered complete after this much idle time.
        //
        // This is deliberately much larger than 2 ms so the receiver tolerates
        // readers that are slower than the nominal Wiegand cadence.
        uint32_t frameGapUs = 20000;

        // Optional frame-length validation. Set minFrameBits to 0 to disable the
        // lower bound. Set maxFrameBits to 0 to disable the upper bound.
        //
        // Example for a 26-bit-only reader:
        //   minFrameBits = 26;
        //   maxFrameBits = 26;
        uint8_t minFrameBits = 0;
        uint8_t maxFrameBits = 64;

        // Wiegand outputs are normally open-collector/open-drain and require
        // pull-ups. Internal pull-ups are convenient for short bench wiring, but
        // external pull-ups to the MCU's logic voltage are preferred in real installs.
        bool useInternalPullups = true;
    };

    struct Frame {
        uint64_t value = 0;
        uint8_t bits = 0;

        // Number of too-close D0/D1 edges discarded while this frame was being read.
        uint16_t rejectedEdges = 0;

        // True if more than 64 accepted bits arrived before the frame ended.
        bool overflow = false;

        uint32_t startedAtUs = 0;
        uint32_t endedAtUs = 0;
    };

    RobustWiegand(uint8_t d0Pin, uint8_t d1Pin);
    ~RobustWiegand();

    // Start listening. Returns false if no interrupt slot is available or either
    // pin cannot be used as an external interrupt on this Arduino core.
    bool begin();
    bool begin(const Config &config);

    void end();

    // Call regularly from loop(). available() and read() also call service().
    // service() closes a frame once frameGapUs of silence has elapsed.
    void service();

    bool available();

    // Returns true and removes one completed frame from the queue.
    bool read(Frame &frame);

    // Drop the current partial frame and all queued completed frames.
    void clear();

    uint32_t totalRejectedEdges() const;
    uint32_t droppedFrames() const;
    uint32_t invalidFrames() const;

private:
    struct QueuedFrame {
        volatile uint64_t value;
        volatile uint8_t bits;
        volatile uint16_t rejectedEdges;
        volatile bool overflow;
        volatile uint32_t startedAtUs;
        volatile uint32_t endedAtUs;
    };

    uint8_t d0Pin_;
    uint8_t d1Pin_;
    int8_t slot_;
    Config config_;
    bool begun_;

    volatile uint64_t currentValue_;
    volatile uint8_t currentBits_;
    volatile uint16_t currentRejectedEdges_;
    volatile bool currentOverflow_;
    volatile uint32_t currentStartedAtUs_;
    volatile uint32_t lastAcceptedAtUs_;

    volatile uint32_t totalRejectedEdges_;
    volatile uint32_t droppedFrames_;
    volatile uint32_t invalidFrames_;

    QueuedFrame queue_[FRAME_QUEUE_SIZE];
    volatile uint8_t queueHead_;
    volatile uint8_t queueTail_;
    volatile uint8_t queueCount_;

    void handleBitFromISR(bool one);
    void finishCurrentFrameFromISR(uint32_t endedAtUs);
    void resetCurrentFromISR();

    bool frameLengthIsValid(uint8_t bits) const;
    void enqueueCurrentFrameFromISR(uint32_t endedAtUs);

    static RobustWiegand *instances_[MAX_READERS];

    static void isr0D0();
    static void isr0D1();
    static void isr1D0();
    static void isr1D1();
    static void isr2D0();
    static void isr2D1();
    static void isr3D0();
    static void isr3D1();

    void attachSlotInterrupts();
    void detachSlotInterrupts();
};