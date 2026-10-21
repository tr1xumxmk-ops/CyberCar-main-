// =============================================================================
// ai_module.h — CYBER CAR PRO — Quantized MLP + Double Q-learning
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "config.h"

// =============================================================================
// 1) QUANTIZED MULTILAYER PERCEPTRON (5 → 8 → 3)
// =============================================================================
class QuantizedMLP {
 public:
  struct Prediction {
    DriveCommand command;
    uint8_t confidence;
  };

  DriveCommand predict(uint16_t front, uint16_t right, uint16_t left) const {
    return predict(front, right, left, 0, 80).command;
  }

  Prediction predict(uint16_t front, uint16_t right, uint16_t left,
                     int16_t deltaCm, uint8_t speedPercent) const {
    int16_t x[5];
    x[0] = static_cast<int16_t>(constrain(front, 0, 200)) * 5;
    x[1] = static_cast<int16_t>(constrain(right, 0, 200)) * 5;
    x[2] = static_cast<int16_t>(constrain(left,  0, 200)) * 5;
    x[3] = static_cast<int16_t>(constrain(deltaCm + 100, 0, 200)) * 5;
    x[4] = static_cast<int16_t>(constrain(speedPercent, 0, 100)) * 10;

    int16_t h[8];
    for (uint8_t j = 0; j < 8; ++j) {
      int32_t sum = static_cast<int32_t>(biasH_[j]) * 8;
      for (uint8_t i = 0; i < 5; ++i) {
        sum += static_cast<int32_t>(x[i]) * wIH_[j][i];
      }
      sum >>= 7;
      if (sum < 0) sum = 0;
      if (sum > 32000) sum = 32000;
      h[j] = static_cast<int16_t>(sum);
    }

    int32_t out[3];
    for (uint8_t k = 0; k < 3; ++k) {
      int32_t sum = static_cast<int32_t>(biasO_[k]) * 8;
      for (uint8_t j = 0; j < 8; ++j) {
        sum += static_cast<int32_t>(h[j]) * wHO_[k][j];
      }
      sum >>= 7;
      out[k] = sum;
    }

    if (front <= Config::SAFE_DISTANCE_CM || right == 0 || left == 0) {
      DriveCommand safe = (right > left) ? DriveCommand::Right : DriveCommand::Left;
      return { safe, 100 };
    }

    uint8_t best = 0;
    for (uint8_t k = 1; k < 3; ++k) if (out[k] > out[best]) best = k;

    int32_t sorted[3] = {out[0], out[1], out[2]};
    if (sorted[1] > sorted[0]) { int32_t t = sorted[0]; sorted[0] = sorted[1]; sorted[1] = t; }
    if (sorted[2] > sorted[1]) { int32_t t = sorted[1]; sorted[1] = sorted[2]; sorted[2] = t; }
    if (sorted[1] > sorted[0]) { int32_t t = sorted[0]; sorted[0] = sorted[1]; sorted[1] = t; }
    int32_t margin = sorted[0] - sorted[1];
    uint8_t conf = static_cast<uint8_t>(constrain(margin / 80, 0L, 100L));

    DriveCommand cmd = DriveCommand::Forward;
    if (best == 1)      cmd = DriveCommand::Left;
    else if (best == 2) cmd = DriveCommand::Right;

    return { cmd, conf };
  }

 private:
  static constexpr int8_t wIH_[8][5] = {
    {   60,   -30,  -30,    20,     5 },
    {  -60,    20,   20,   -40,    -5 },
    {    0,    50,  -50,     0,     0 },
    {    0,   -50,   50,     0,     0 },
    {   30,    10,   10,   -10,    10 },
    {  -30,    -5,   -5,     0,     0 },
    {   10,    30,   30,    10,     5 },
    {    0,     0,    0,    20,    -5 },
  };
  static constexpr int8_t biasH_[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  static constexpr int8_t wHO_[3][8] = {
    {   80,  -60,    0,    0,   40,  -30,   20,    0 },
    {  -20,   20,  -60,   60,    0,    0,   10,    0 },
    {  -20,   20,   60,  -60,    0,    0,   10,    0 },
  };
  static constexpr int8_t biasO_[3] = {20, 0, 0};
};

// =============================================================================
// 2) DOUBLE Q-LEARNING AGENT (8 states × 5 actions + replay + telemetry)
// =============================================================================
class QuantizedDoubleQLearning {
 public:
  static constexpr uint8_t kStates     = 8;
  static constexpr uint8_t kActions    = 5;
  static constexpr uint8_t kReplaySize = 24;

  uint8_t stateOf(uint16_t front, bool fresh, int16_t deltaCm) const {
    if (!fresh) return 0;
    uint8_t zone;
    if      (front <= Config::CRITICAL_DISTANCE_CM) zone = 0;
    else if (front <= Config::SAFE_DISTANCE_CM)     zone = 1;
    else if (front <= 50)                            zone = 2;
    else                                             zone = 3;
    uint8_t trend = (deltaCm < -2) ? 1 : 0;
    return static_cast<uint8_t>((zone << 1) | trend);
  }

  uint8_t act(uint8_t s) const {
    if (s >= kStates) return 4;
    if (random(1000) < epsilonPerMille_) {
      return static_cast<uint8_t>(random(kActions));
    }
    uint8_t best = 0;
    int32_t bestValue = static_cast<int32_t>(q1_[s][0]) + q2_[s][0];
    for (uint8_t a = 1; a < kActions; ++a) {
      int32_t v = static_cast<int32_t>(q1_[s][a]) + q2_[s][a];
      if (v > bestValue) { bestValue = v; best = a; }
    }
    return best;
  }

  void learn(uint8_t s, uint8_t a, int16_t reward, uint8_t ns) {
    if (s >= kStates || a >= kActions || ns >= kStates) return;

    replay_[replayIdx_] = Transition{s, a, reward, ns};
    replayIdx_ = static_cast<uint8_t>((replayIdx_ + 1) % kReplaySize);
    if (replayCount_ < kReplaySize) ++replayCount_;

    doubleQUpdate(s, a, reward, ns);

    if (replayCount_ >= 4 && (updateCount_ % 3) == 0) {
      const Transition& t = replay_[random(replayCount_)];
      doubleQUpdate(t.s, t.a, t.r, t.ns);
    }

    ++updateCount_;
    if (updateCount_ % 50 == 0 && epsilonPerMille_ > 50) {
      epsilonPerMille_ = static_cast<uint16_t>(epsilonPerMille_ - 10);
    }

    episodeReward_ += reward;
    if (reward <= -100) endEpisode();

    dirty_ = true;
  }

  void begin() {
    prefsReady_ = prefs_.begin("cyber-car-rl", false);
    if (prefsReady_) loadFromNvs();
    else             initDefaultQ();
  }

  void update(uint32_t now) {
    if (!prefsReady_ || !dirty_) return;
    if (!elapsed(now, lastSaveAt_, 20000)) return;
    saveToNvs();
    lastSaveAt_ = now;
    dirty_ = false;
  }

  uint16_t updateCount() const       { return updateCount_; }
  uint16_t epsilonPerMille() const   { return epsilonPerMille_; }
  int32_t  episodeReward() const     { return episodeReward_; }
  int32_t  bestEpisodeReward() const { return bestEpisodeReward_; }
  uint16_t episodeCount() const      { return episodeCount_; }

 private:
  struct Transition { uint8_t s, a, ns; int16_t r; };

  void doubleQUpdate(uint8_t s, uint8_t a, int16_t reward, uint8_t ns) {
    if (random(2) == 0) {
      uint8_t bestNext = 0;
      for (uint8_t i = 1; i < kActions; ++i)
        if (q1_[ns][i] > q1_[ns][bestNext]) bestNext = i;
      int32_t target = reward + (q2_[ns][bestNext] * kGamma) / 1000;
      q1_[s][a] = static_cast<int16_t>(constrain(
          q1_[s][a] + (target - q1_[s][a]) * kAlpha / 1000, -3000L, 3000L));
    } else {
      uint8_t bestNext = 0;
      for (uint8_t i = 1; i < kActions; ++i)
        if (q2_[ns][i] > q2_[ns][bestNext]) bestNext = i;
      int32_t target = reward + (q1_[ns][bestNext] * kGamma) / 1000;
      q2_[s][a] = static_cast<int16_t>(constrain(
          q2_[s][a] + (target - q2_[s][a]) * kAlpha / 1000, -3000L, 3000L));
    }
  }

  void endEpisode() {
    if (episodeReward_ > bestEpisodeReward_) bestEpisodeReward_ = episodeReward_;
    ++episodeCount_;
    episodeReward_ = 0;
    dirty_ = true;
  }

  void initDefaultQ() {
    for (uint8_t s = 0; s < kStates; ++s) {
      for (uint8_t a = 0; a < kActions; ++a) {
        q1_[s][a] = 10;
        q2_[s][a] = 10;
      }
    }
    for (uint8_t s = 0; s <= 1; ++s) {
      q1_[s][0] = q2_[s][0] = -100;
      q1_[s][1] = q2_[s][1] =   80;
      q1_[s][2] = q2_[s][2] =   80;
      q1_[s][3] = q2_[s][3] =  100;
      q1_[s][4] = q2_[s][4] =  100;
    }
    for (uint8_t s = 4; s < kStates; ++s) {
      q1_[s][0] = q2_[s][0] = 80;
    }
  }

  static constexpr size_t kBlobSize = sizeof(int16_t) * kStates * kActions * 2 + 12;

  void loadFromNvs() {
    uint8_t buf[kBlobSize];
    if (prefs_.getBytesLength("rlstate") != kBlobSize) { initDefaultQ(); return; }
    if (prefs_.getBytes("rlstate", buf, kBlobSize) != kBlobSize) { initDefaultQ(); return; }
    size_t off = 0;
    memcpy(q1_, buf + off, sizeof(q1_));                    off += sizeof(q1_);
    memcpy(q2_, buf + off, sizeof(q2_));                    off += sizeof(q2_);
    memcpy(&epsilonPerMille_,   buf + off, 2);              off += 2;
    memcpy(&updateCount_,       buf + off, 2);              off += 2;
    memcpy(&bestEpisodeReward_, buf + off, 4);              off += 4;
    memcpy(&episodeCount_,      buf + off, 2);
  }

  void saveToNvs() {
    uint8_t buf[kBlobSize];
    size_t off = 0;
    memcpy(buf + off, q1_, sizeof(q1_));                    off += sizeof(q1_);
    memcpy(buf + off, q2_, sizeof(q2_));                    off += sizeof(q2_);
    memcpy(buf + off, &epsilonPerMille_, 2);                off += 2;
    memcpy(buf + off, &updateCount_, 2);                    off += 2;
    memcpy(buf + off, &bestEpisodeReward_, 4);              off += 4;
    memcpy(buf + off, &episodeCount_, 2);
    prefs_.putBytes("rlstate", buf, kBlobSize);
  }

  Preferences prefs_;
  int16_t q1_[kStates][kActions] = {};
  int16_t q2_[kStates][kActions] = {};
  Transition replay_[kReplaySize] = {};
  uint8_t  replayIdx_        = 0;
  uint8_t  replayCount_      = 0;
  uint16_t epsilonPerMille_  = 500;
  uint16_t updateCount_      = 0;
  int32_t  episodeReward_    = 0;
  int32_t  bestEpisodeReward_ = -10000;
  uint16_t episodeCount_     = 0;
  bool     prefsReady_       = false;
  bool     dirty_            = false;
  uint32_t lastSaveAt_       = 0;

  static constexpr int32_t kGamma = 900;
  static constexpr int32_t kAlpha = 200;
};

using QuantizedRL = QuantizedDoubleQLearning;