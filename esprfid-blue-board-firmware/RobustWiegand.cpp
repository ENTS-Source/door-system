#include "RobustWiegand.h"

#if defined(ESP8266) || defined(ESP32)
#define RW_ISR_ATTR IRAM_ATTR
#else
#define RW_ISR_ATTR
#endif

RobustWiegand *RobustWiegand::instances_[RobustWiegand::MAX_READERS] = {
    nullptr, nullptr, nullptr, nullptr
};

RobustWiegand::RobustWiegand(uint8_t d0Pin, uint8_t d1Pin)
    : d0Pin_(d0Pin),
      d1Pin_(d1Pin),
      slot_(-1),
      begun_(false),
      currentValue_(0),
      currentBits_(0),
      currentRejectedEdges_(0),
      currentOverflow_(false),
      currentStartedAtUs_(0),
      lastAcceptedAtUs_(0),
      totalRejectedEdges_(0),
      droppedFrames_(0),
      invalidFrames_(0),
      queueHead_(0),
      queueTail_(0),
      queueCount_(0) {
}

RobustWiegand::~RobustWiegand() {
    end();
}

bool RobustWiegand::begin() {
    Config config;
    return begin(config);
}

bool RobustWiegand::begin(const Config &config) {
    if (begun_) {
        return true;
    }

    const int d0Interrupt = digitalPinToInterrupt(d0Pin_);
    const int d1Interrupt = digitalPinToInterrupt(d1Pin_);

#if defined(NOT_AN_INTERRUPT)
    if (d0Interrupt == NOT_AN_INTERRUPT || d1Interrupt == NOT_AN_INTERRUPT) {
        return false;
    }
#else
    if (d0Interrupt < 0 || d1Interrupt < 0) {
        return false;
    }
#endif

    config_ = config;

    // Guard against nonsensical settings.
    if (config_.frameGapUs <= config_.minBitSpacingUs) {
        config_.frameGapUs = config_.minBitSpacingUs + 1000;
    }
    if (config_.maxFrameBits > 64) {
        config_.maxFrameBits = 64;
    }
    if (config_.maxFrameBits != 0 &&
        config_.minFrameBits > config_.maxFrameBits) {
        const uint8_t temp = config_.minFrameBits;
        config_.minFrameBits = config_.maxFrameBits;
        config_.maxFrameBits = temp;
    }

    pinMode(d0Pin_, config_.useInternalPullups ? INPUT_PULLUP : INPUT);
    pinMode(d1Pin_, config_.useInternalPullups ? INPUT_PULLUP : INPUT);

    noInterrupts();

    for (uint8_t i = 0; i < MAX_READERS; ++i) {
        if (instances_[i] == nullptr) {
            slot_ = static_cast<int8_t>(i);
            instances_[i] = this;
            break;
        }
    }

    if (slot_ < 0) {
        interrupts();
        return false;
    }

    currentValue_ = 0;
    currentBits_ = 0;
    currentRejectedEdges_ = 0;
    currentOverflow_ = false;
    currentStartedAtUs_ = 0;
    lastAcceptedAtUs_ = 0;

    queueHead_ = 0;
    queueTail_ = 0;
    queueCount_ = 0;

    interrupts();

    attachSlotInterrupts();
    begun_ = true;
    return true;
}

void RobustWiegand::end() {
    if (!begun_) {
        return;
    }

    detachSlotInterrupts();

    noInterrupts();
    if (slot_ >= 0 && slot_ < static_cast<int8_t>(MAX_READERS) &&
        instances_[slot_] == this) {
        instances_[slot_] = nullptr;
    }
    slot_ = -1;
    begun_ = false;
    interrupts();
}

void RobustWiegand::service() {
    if (!begun_) {
        return;
    }

    const uint32_t now = micros();

    noInterrupts();

    if (currentBits_ > 0) {
        const uint32_t idleUs = now - lastAcceptedAtUs_;
        if (idleUs >= config_.frameGapUs) {
            finishCurrentFrameFromISR(lastAcceptedAtUs_);
        }
    }

    interrupts();
}

bool RobustWiegand::available() {
    service();

    noInterrupts();
    const bool result = (queueCount_ != 0);
    interrupts();

    return result;
}

bool RobustWiegand::read(Frame &frame) {
    service();

    noInterrupts();

    if (queueCount_ == 0) {
        interrupts();
        return false;
    }

    // Copy field-by-field. This avoids assigning from a volatile struct and also
    // keeps the operation safe on 8/32-bit MCUs while interrupts are disabled.
    frame.value = queue_[queueHead_].value;
    frame.bits = queue_[queueHead_].bits;
    frame.rejectedEdges = queue_[queueHead_].rejectedEdges;
    frame.overflow = queue_[queueHead_].overflow;
    frame.startedAtUs = queue_[queueHead_].startedAtUs;
    frame.endedAtUs = queue_[queueHead_].endedAtUs;

    queueHead_ = static_cast<uint8_t>((queueHead_ + 1) % FRAME_QUEUE_SIZE);
    --queueCount_;

    interrupts();
    return true;
}

void RobustWiegand::clear() {
    noInterrupts();

    resetCurrentFromISR();

    queueHead_ = 0;
    queueTail_ = 0;
    queueCount_ = 0;

    interrupts();
}

uint32_t RobustWiegand::totalRejectedEdges() const {
    noInterrupts();
    const uint32_t value = totalRejectedEdges_;
    interrupts();
    return value;
}

uint32_t RobustWiegand::droppedFrames() const {
    noInterrupts();
    const uint32_t value = droppedFrames_;
    interrupts();
    return value;
}

uint32_t RobustWiegand::invalidFrames() const {
    noInterrupts();
    const uint32_t value = invalidFrames_;
    interrupts();
    return value;
}

void RW_ISR_ATTR RobustWiegand::handleBitFromISR(bool one) {
    const uint32_t now = micros();

    if (currentBits_ > 0) {
        const uint32_t sinceLastAcceptedUs = now - lastAcceptedAtUs_;

        // A long quiet period means the previous frame ended. Close it before
        // considering this edge as the first bit of a new frame.
        if (sinceLastAcceptedUs >= config_.frameGapUs) {
            finishCurrentFrameFromISR(lastAcceptedAtUs_);
        } else if (sinceLastAcceptedUs < config_.minBitSpacingUs) {
            // This is the main ringing/chatter filter. It is intentionally only
            // a minimum-spacing test: bits that arrive later than the nominal
            // ~2 ms Wiegand period are still accepted.
            ++currentRejectedEdges_;
            ++totalRejectedEdges_;
            return;
        }
    }

    if (currentBits_ == 0) {
        currentStartedAtUs_ = now;
        currentValue_ = 0;
        currentRejectedEdges_ = 0;
        currentOverflow_ = false;
    }

    lastAcceptedAtUs_ = now;

    if (currentBits_ < 64) {
        currentValue_ = (currentValue_ << 1) | (one ? 1ULL : 0ULL);
        ++currentBits_;
    } else {
        currentOverflow_ = true;
    }
}

void RW_ISR_ATTR RobustWiegand::finishCurrentFrameFromISR(uint32_t endedAtUs) {
    if (currentBits_ == 0) {
        return;
    }

    if (frameLengthIsValid(currentBits_)) {
        enqueueCurrentFrameFromISR(endedAtUs);
    } else {
        ++invalidFrames_;
    }

    resetCurrentFromISR();
}

void RW_ISR_ATTR RobustWiegand::resetCurrentFromISR() {
    currentValue_ = 0;
    currentBits_ = 0;
    currentRejectedEdges_ = 0;
    currentOverflow_ = false;
    currentStartedAtUs_ = 0;
    lastAcceptedAtUs_ = 0;
}

bool RW_ISR_ATTR RobustWiegand::frameLengthIsValid(uint8_t bits) const {
    if (config_.minFrameBits != 0 && bits < config_.minFrameBits) {
        return false;
    }

    if (config_.maxFrameBits != 0 && bits > config_.maxFrameBits) {
        return false;
    }

    return true;
}

void RW_ISR_ATTR RobustWiegand::enqueueCurrentFrameFromISR(uint32_t endedAtUs) {
    if (queueCount_ >= FRAME_QUEUE_SIZE) {
        ++droppedFrames_;
        return;
    }

    queue_[queueTail_].value = currentValue_;
    queue_[queueTail_].bits = currentBits_;
    queue_[queueTail_].rejectedEdges = currentRejectedEdges_;
    queue_[queueTail_].overflow = currentOverflow_;
    queue_[queueTail_].startedAtUs = currentStartedAtUs_;
    queue_[queueTail_].endedAtUs = endedAtUs;

    queueTail_ = static_cast<uint8_t>((queueTail_ + 1) % FRAME_QUEUE_SIZE);
    ++queueCount_;
}

void RobustWiegand::attachSlotInterrupts() {
    switch (slot_) {
        case 0:
            attachInterrupt(digitalPinToInterrupt(d0Pin_), isr0D0, FALLING);
            attachInterrupt(digitalPinToInterrupt(d1Pin_), isr0D1, FALLING);
            break;
        case 1:
            attachInterrupt(digitalPinToInterrupt(d0Pin_), isr1D0, FALLING);
            attachInterrupt(digitalPinToInterrupt(d1Pin_), isr1D1, FALLING);
            break;
        case 2:
            attachInterrupt(digitalPinToInterrupt(d0Pin_), isr2D0, FALLING);
            attachInterrupt(digitalPinToInterrupt(d1Pin_), isr2D1, FALLING);
            break;
        case 3:
            attachInterrupt(digitalPinToInterrupt(d0Pin_), isr3D0, FALLING);
            attachInterrupt(digitalPinToInterrupt(d1Pin_), isr3D1, FALLING);
            break;
        default:
            break;
    }
}

void RobustWiegand::detachSlotInterrupts() {
    detachInterrupt(digitalPinToInterrupt(d0Pin_));
    detachInterrupt(digitalPinToInterrupt(d1Pin_));
}

void RW_ISR_ATTR RobustWiegand::isr0D0() {
    if (instances_[0] != nullptr) instances_[0]->handleBitFromISR(false);
}

void RW_ISR_ATTR RobustWiegand::isr0D1() {
    if (instances_[0] != nullptr) instances_[0]->handleBitFromISR(true);
}

void RW_ISR_ATTR RobustWiegand::isr1D0() {
    if (instances_[1] != nullptr) instances_[1]->handleBitFromISR(false);
}

void RW_ISR_ATTR RobustWiegand::isr1D1() {
    if (instances_[1] != nullptr) instances_[1]->handleBitFromISR(true);
}

void RW_ISR_ATTR RobustWiegand::isr2D0() {
    if (instances_[2] != nullptr) instances_[2]->handleBitFromISR(false);
}

void RW_ISR_ATTR RobustWiegand::isr2D1() {
    if (instances_[2] != nullptr) instances_[2]->handleBitFromISR(true);
}

void RW_ISR_ATTR RobustWiegand::isr3D0() {
    if (instances_[3] != nullptr) instances_[3]->handleBitFromISR(false);
}

void RW_ISR_ATTR RobustWiegand::isr3D1() {
    if (instances_[3] != nullptr) instances_[3]->handleBitFromISR(true);
}