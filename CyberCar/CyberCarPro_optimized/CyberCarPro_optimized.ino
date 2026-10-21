// =============================================================================
// CyberCarPro_optimized.ino — CYBER CAR PRO MAIN SKETCH
// =============================================================================
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "ai_module.h"

// =============================================================================
// HARDWARE CLASSES
// =============================================================================
class StatusLED {
 public:
  StatusLED(uint8_t r, uint8_t g, uint8_t b) : r_(r), g_(g), b_(b) {}
  void begin() { pinMode(r_,OUTPUT); pinMode(g_,OUTPUT); pinMode(b_,OUTPUT); off(); }
  void off()        { set(false,false,false); }
  void setRed()     { set(true,false,false); }
  void setGreen()   { set(false,true,false); }
  void setBlue()    { set(false,false,true); }
  void setMagenta() { set(true,false,true); }
  void setYellow()  { set(true,true,false); }
  void setCyan()    { set(false,true,true); }
 private:
  void set(bool r,bool g,bool b){ digitalWrite(r_,r?HIGH:LOW); digitalWrite(g_,g?HIGH:LOW); digitalWrite(b_,b?HIGH:LOW); }
  uint8_t r_,g_,b_;
};

class SoundManager {
 public:
  explicit SoundManager(uint8_t pin) : pin_(pin) {}
  void begin(){ pinMode(pin_,OUTPUT); noTone(pin_); }
  void update(uint32_t now){
    if(!active_ || !elapsed(now,startedAt_,currentStepMs_)) return;
    if(sequence_ && (stepIndex_+1U) < stepCount_){ ++stepIndex_; startStep(now); return; }
    noTone(pin_); clearPlayback();
  }
  void beep(uint16_t f, uint16_t d){ playSingle(f,d); }
  void horn(){ static const ToneStep k[] = {{900,100,120},{900,100,120},{900,150,150}}; playSequence(k,3); }
  void brake(){ playSingle(280,450); }
  void releaseSound(){ playSingle(1200,180); }
  void danceSound(){ static const ToneStep k[] = {{659,80,80},{784,80,80},{988,120,120}}; playSequence(k,3); }
 private:
  struct ToneStep { uint16_t frequency, toneMs, stepMs; };
  void playSingle(uint16_t f, uint16_t d){
    sequence_=nullptr; stepCount_=0; stepIndex_=0; active_=true;
    startedAt_=millis(); currentStepMs_=d; tone(pin_,f,d);
  }
  void playSequence(const ToneStep* s, uint8_t n){
    if(!s || !n) return;
    sequence_=s; stepCount_=n; stepIndex_=0; active_=true; startStep(millis());
  }
  void startStep(uint32_t now){
    const ToneStep& s = sequence_[stepIndex_];
    startedAt_=now; currentStepMs_=s.stepMs; tone(pin_,s.frequency,s.toneMs);
  }
  void clearPlayback(){ active_=false; sequence_=nullptr; stepCount_=0; stepIndex_=0; }
  uint8_t pin_; bool active_=false;
  uint32_t startedAt_=0; uint16_t currentStepMs_=0;
  const ToneStep* sequence_=nullptr; uint8_t stepCount_=0, stepIndex_=0;
};

class MotorDriver {
 public:
  MotorDriver(uint8_t in1,uint8_t in2,uint8_t in3,uint8_t in4,uint8_t ena,uint8_t enb)
      : in1_(in1),in2_(in2),in3_(in3),in4_(in4),ena_(ena),enb_(enb) {}
  void begin(){
    pinMode(in1_,OUTPUT); pinMode(in2_,OUTPUT);
    pinMode(in3_,OUTPUT); pinMode(in4_,OUTPUT);
    ledcAttach(ena_,5000,8); ledcAttach(enb_,5000,8);
    setSpeed(80);
    setPins(LOW,LOW,LOW,LOW);
    activeCmd_=DriveCommand::Stop; pinsInitialized_=true;
  }
  void setSpeed(uint8_t p){
    speedLimit_=(uint8_t)constrain(p,20,100);
    uint8_t duty=(uint8_t)(((uint16_t)speedLimit_*255U)/100U);
    ledcWrite(ena_,duty); ledcWrite(enb_,duty);
  }
  void apply(DriveCommand c){
    if(pinsInitialized_ && c==activeCmd_) return;
    switch(c){
      case DriveCommand::Forward:  setPins(HIGH,LOW,HIGH,LOW); break;
      case DriveCommand::Backward: setPins(LOW,HIGH,LOW,HIGH); break;
      case DriveCommand::Left:     setPins(LOW,HIGH,HIGH,LOW); break;
      case DriveCommand::Right:    setPins(HIGH,LOW,LOW,HIGH); break;
      case DriveCommand::Brake:    setPins(HIGH,HIGH,HIGH,HIGH); break;
      case DriveCommand::Stop:     setPins(LOW,LOW,LOW,LOW); break;
    }
    activeCmd_=c; pinsInitialized_=true;
  }
  void stop(){ apply(DriveCommand::Stop); }
  void brake(){ apply(DriveCommand::Brake); }
  uint8_t speedLimit() const { return speedLimit_; }
  DriveCommand activeCommand() const { return activeCmd_; }
 private:
  void setPins(uint8_t a,uint8_t b,uint8_t c,uint8_t d){
    digitalWrite(in1_,a); digitalWrite(in2_,b); digitalWrite(in3_,c); digitalWrite(in4_,d);
  }
  uint8_t in1_,in2_,in3_,in4_,ena_,enb_;
  uint8_t speedLimit_=80;
  DriveCommand activeCmd_=DriveCommand::Stop;
  bool pinsInitialized_=false;
};

class DistanceSensor {
 public:
  DistanceSensor(uint8_t trig, uint8_t echo) : trig_(trig), echo_(echo) {}
  void begin(){ pinMode(trig_,OUTPUT); pinMode(echo_,INPUT); digitalWrite(trig_,LOW); }
  void update(uint32_t now){
    if(!elapsed(now,lastReadAt_,Config::SENSOR_INTERVAL_MS)) return;
    lastReadAt_=now;
    digitalWrite(trig_,LOW); delayMicroseconds(2);
    digitalWrite(trig_,HIGH); delayMicroseconds(10);
    digitalWrite(trig_,LOW);
    uint32_t d = pulseIn(echo_, HIGH, Config::ULTRASONIC_TIMEOUT_US);
    if(d==0){ valid_=false; return; }
    uint16_t cm = (uint16_t)((d*0.034f)/2.0f);
    samples_[sampleIndex_]=cm;
    sampleIndex_=(sampleIndex_+1U)%Config::SENSOR_FILTER_SAMPLES;
    if(sampleCount_<Config::SENSOR_FILTER_SAMPLES) ++sampleCount_;
    uint16_t sorted[Config::SENSOR_FILTER_SAMPLES];
    for(uint8_t i=0;i<sampleCount_;++i) sorted[i]=samples_[i];
    for(uint8_t i=0;i<sampleCount_;++i)
      for(uint8_t j=i+1;j<sampleCount_;++j)
        if(sorted[j]<sorted[i]){ uint16_t t=sorted[i]; sorted[i]=sorted[j]; sorted[j]=t; }
    distanceCm_=sorted[(sampleCount_-1U)/2U];
    valid_=true;
  }
  void resetFilter(){ sampleIndex_=0; sampleCount_=0; valid_=false; }
  bool isFresh(uint32_t now) const { return valid_ && !elapsed(now,lastReadAt_,Config::SENSOR_STALE_MS); }
  uint16_t distanceCm() const { return distanceCm_; }
 private:
  uint8_t trig_, echo_;
  uint32_t lastReadAt_=0;
  uint16_t samples_[Config::SENSOR_FILTER_SAMPLES]={};
  uint8_t sampleIndex_=0, sampleCount_=0;
  uint16_t distanceCm_=999;
  bool valid_=false;
};

class DualServoSystem {
 public:
  DualServoSystem(uint8_t s, uint8_t b) : scanPin_(s), ballPin_(b) {}
  void begin(){
    scan_.setPeriodHertz(50); scan_.attach(scanPin_,500,2400);
    ball_.setPeriodHertz(50); ball_.attach(ballPin_,500,2400);
    centerScan(); lockBall();
  }
  void setScanAngle(uint8_t a){ scan_.write((uint8_t)constrain(a,0,180)); }
  void centerScan(){ setScanAngle(90); }
  void lockBall(){ ball_.write(Config::BALL_LOCKED_ANGLE); locked_=true; }
  void releaseBall(){ ball_.write(Config::BALL_RELEASE_ANGLE); locked_=false; }
  bool isBallLocked() const { return locked_; }
 private:
  uint8_t scanPin_, ballPin_;
  Servo scan_, ball_;
  bool locked_=true;
};

// =============================================================================
// CAR NAVIGATOR
// =============================================================================
class CarNavigator {
 public:
  CarNavigator(MotorDriver& m, DistanceSensor& s, DualServoSystem& sv,
               StatusLED& led, SoundManager& snd)
      : motors_(m), sensor_(s), servos_(sv), led_(led), sound_(snd) {}

  void begin(uint32_t now){
    rlAgent_.begin();
    lastManualAt_=now;
    applyLed();
    danceAt_=now+800;
  }
  void update(uint32_t now){
    if(danceAt_!=0 && deadlineReached(now,danceAt_)){ danceAt_=0; startDance(now); }
    if(!emergencyLatched_){
      updateDance(now);
      if(!dancing_){
        switch(mode_){
          case OperatingMode::Manual: updateManual(now); break;
          case OperatingMode::Autonomous: updateAuto(now); break;
          case OperatingMode::ReinforcementLearning: updateRL(now); break;
        }
      }
    }
    rlAgent_.update(now);
  }
  bool setMode(OperatingMode m){
    uint32_t now=millis();
    cancelDance(); motors_.stop(); servos_.centerScan(); sensor_.resetFilter();
    mode_=m; manualCmd_=DriveCommand::Stop; lastManualAt_=now;
    resetAutoState(); resetRlState(now);
    emergencyLatched_=false; safetyStop_=false;
    applyLed(); sound_.beep(1000,60);
    return true;
  }
  bool handleManual(char c, uint32_t now){
    if(mode_!=OperatingMode::Manual || dancing_) return false;
    DriveCommand newCmd=DriveCommand::Stop;
    switch(c){
      case 'F': newCmd=DriveCommand::Forward; break;
      case 'B': newCmd=DriveCommand::Backward; break;
      case 'L': newCmd=DriveCommand::Left; break;
      case 'R': newCmd=DriveCommand::Right; break;
      case 'S': newCmd=DriveCommand::Stop; break;
      default: return false;
    }
    if(emergencyLatched_){
      if(newCmd!=DriveCommand::Stop) return false;
      emergencyLatched_=false; safetyStop_=false;
      motors_.stop(); applyLed();
    }
    manualCmd_=newCmd; lastManualAt_=now;
    return true;
  }
  void emergencyStop(){
    cancelDance(); rlActive_=false;
    manualCmd_=DriveCommand::Stop;
    motors_.brake(); sound_.brake(); led_.setRed();
    safetyStop_=true; emergencyLatched_=true;
  }
  bool startDance(uint32_t now){
    if(dancing_||emergencyLatched_) return false;
    motors_.stop(); dancing_=true; danceStep_=0; danceStartedAt_=now;
    led_.setYellow(); sound_.danceSound();
    return true;
  }
  void horn(){ sound_.horn(); }
  void releaseBall(){ servos_.releaseBall(); sound_.releaseSound(); }
  void lockBall(){ servos_.lockBall(); }
  const char* modeName() const {
    switch(mode_){
      case OperatingMode::Manual: return "manual";
      case OperatingMode::Autonomous: return "auto";
      case OperatingMode::ReinforcementLearning: return "rl";
    }
    return "manual";
  }
  const char* commandName() const {
    switch(motors_.activeCommand()){
      case DriveCommand::Forward: return "forward";
      case DriveCommand::Backward: return "backward";
      case DriveCommand::Left: return "left";
      case DriveCommand::Right: return "right";
      case DriveCommand::Brake: return "brake";
      case DriveCommand::Stop: return "stop";
    }
    return "stop";
  }
  bool safetyStopped() const { return safetyStop_; }
  bool sensorFresh(uint32_t now) const { return sensor_.isFresh(now); }
  uint16_t distance() const { return sensor_.distanceCm(); }
  MotorDriver& motors(){ return motors_; }
  bool ballLocked() const { return servos_.isBallLocked(); }

  // RL telemetry
  uint16_t rlUpdates() const        { return rlAgent_.updateCount(); }
  uint16_t rlEpsilonPerMille() const{ return rlAgent_.epsilonPerMille(); }
  int32_t  rlBestEpisode() const    { return rlAgent_.bestEpisodeReward(); }
  uint16_t rlEpisodeCount() const   { return rlAgent_.episodeCount(); }

 private:
  enum class AutoPhase : uint8_t { Cruise, Reverse, ScanRight, ScanLeft, Turn };

  void updateDance(uint32_t now){
    if(!dancing_) return;
    static const uint16_t kSteps[] = {120,120,100,100,100,100,100,100};
    if(!elapsed(now,danceStartedAt_,kSteps[danceStep_])) return;
    ++danceStep_; danceStartedAt_=now;
    switch(danceStep_){
      case 1: servos_.setScanAngle(45); break;
      case 2: servos_.setScanAngle(135); break;
      case 3: servos_.centerScan(); break;
      case 4: motors_.apply(DriveCommand::Left); break;
      case 5: motors_.apply(DriveCommand::Right); break;
      case 6: motors_.apply(DriveCommand::Left); break;
      case 7: motors_.apply(DriveCommand::Right); break;
      default: motors_.stop(); servos_.centerScan(); dancing_=false; applyLed(); break;
    }
  }

  bool obstacle(uint32_t now) const {
    return !sensor_.isFresh(now) || sensor_.distanceCm()<=Config::SAFE_DISTANCE_CM;
  }
  void engageObstacleBrake(){
    if(!safetyStop_) sound_.brake();
    safetyStop_=true; motors_.brake();
  }
  void updateManual(uint32_t now){
    if(elapsed(now,lastManualAt_,Config::MANUAL_TIMEOUT_MS)){
      manualCmd_=DriveCommand::Stop; safetyStop_=true; motors_.stop(); return;
    }
    if(manualCmd_==DriveCommand::Forward && obstacle(now)){ engageObstacleBrake(); return; }
    safetyStop_=false; motors_.apply(manualCmd_);
  }
  void updateAuto(uint32_t now){
    switch(autoPhase_){
      case AutoPhase::Cruise:
        if(obstacle(now)){
          motors_.apply(DriveCommand::Backward);
          autoPhase_=AutoPhase::Reverse; autoStartedAt_=now; safetyStop_=true;
        } else { motors_.apply(DriveCommand::Forward); safetyStop_=false; }
        break;
      case AutoPhase::Reverse:
        if(elapsed(now,autoStartedAt_,Config::AUTO_REVERSE_MS)){
          motors_.stop(); servos_.setScanAngle(18); sensor_.resetFilter();
          autoPhase_=AutoPhase::ScanRight; autoStartedAt_=now;
        } break;
      case AutoPhase::ScanRight:
        if(elapsed(now,autoStartedAt_,Config::AUTO_SCAN_SETTLE_MS)){
          rightDistance_ = sensor_.isFresh(now)?sensor_.distanceCm():0;
          servos_.setScanAngle(162); sensor_.resetFilter();
          autoPhase_=AutoPhase::ScanLeft; autoStartedAt_=now;
        } break;
      case AutoPhase::ScanLeft:
        if(elapsed(now,autoStartedAt_,Config::AUTO_SCAN_SETTLE_MS)){
          leftDistance_ = sensor_.isFresh(now)?sensor_.distanceCm():0;
          servos_.centerScan();
          if(rightDistance_>Config::SAFE_DISTANCE_CM &&
             (leftDistance_<=Config::SAFE_DISTANCE_CM || rightDistance_>=leftDistance_)){
            motors_.apply(DriveCommand::Right); turnDurationMs_=Config::AUTO_TURN_MS;
          } else if(leftDistance_>Config::SAFE_DISTANCE_CM){
            motors_.apply(DriveCommand::Left);  turnDurationMs_=Config::AUTO_TURN_MS;
          } else {
            motors_.apply(DriveCommand::Right); turnDurationMs_=Config::AUTO_U_TURN_MS;
          }
          autoPhase_=AutoPhase::Turn; autoStartedAt_=now;
        } break;
      case AutoPhase::Turn:
        if(elapsed(now,autoStartedAt_,turnDurationMs_)){
          motors_.stop(); autoPhase_=AutoPhase::Cruise;
        } break;
    }
  }

  void updateRL(uint32_t now){
    if(rlActive_){
      if(!elapsed(now,rlStartedAt_,rlDurationMs_)) return;
      uint16_t currentDist = sensor_.distanceCm();
      bool     fresh       = sensor_.isFresh(now);
      int16_t  delta       = (int16_t)currentDist - (int16_t)rlDistanceBefore_;
      uint8_t  nextState   = rlAgent_.stateOf(currentDist, fresh, delta);

      int16_t reward = 0;
      if(!fresh || currentDist <= Config::CRITICAL_DISTANCE_CM) reward = -150;
      else if(delta > 5)  reward =  40;
      else if(delta < -5) reward = -10;
      else                reward =   5;
      reward -= 1;
      if(rlAction_==3 && currentDist > Config::SAFE_DISTANCE_CM) reward -= 5;
      if(currentDist > 50 && rlAction_==0)                       reward += 20;

      rlAgent_.learn(rlState_, rlAction_, reward, nextState);
      motors_.stop();
      rlActive_=false;
      lastRlAt_=now;
      return;
    }

    if(!elapsed(now,lastRlAt_,Config::RL_ACTION_MS)) return;

    rlDistanceBefore_ = sensor_.distanceCm();
    bool fresh = sensor_.isFresh(now);

    int16_t delta = 0;
    if(rlHistoryCount_ > 0){
      delta = (int16_t)rlDistanceBefore_ - (int16_t)rlDistanceHistory_[0];
    }
    for(int8_t i=2;i>0;--i) rlDistanceHistory_[i]=rlDistanceHistory_[i-1];
    rlDistanceHistory_[0]=rlDistanceBefore_;
    if(rlHistoryCount_<3) ++rlHistoryCount_;

    rlState_  = rlAgent_.stateOf(rlDistanceBefore_, fresh, delta);
    rlAction_ = rlAgent_.act(rlState_);
    if(!fresh || rlDistanceBefore_ <= Config::SAFE_DISTANCE_CM) rlAction_=3;

    switch(rlAction_){
      case 0: motors_.apply(DriveCommand::Forward);  rlDurationMs_=Config::RL_ACTION_MS;  break;
      case 1: motors_.apply(DriveCommand::Left);     rlDurationMs_=Config::RL_TURN_MS;    break;
      case 2: motors_.apply(DriveCommand::Right);    rlDurationMs_=Config::RL_TURN_MS;    break;
      case 3: motors_.apply(DriveCommand::Backward); rlDurationMs_=Config::RL_REVERSE_MS; break;
      case 4: motors_.brake();                       rlDurationMs_=100;                   break;
    }
    rlStartedAt_=now;
    rlActive_=true;
  }

  void cancelDance(){ if(!dancing_) return; dancing_=false; motors_.stop(); }
  void resetAutoState(){
    autoPhase_=AutoPhase::Cruise; autoStartedAt_=0;
    rightDistance_=0; leftDistance_=0; turnDurationMs_=0;
  }
  void resetRlState(uint32_t now){
    rlActive_=false; rlStartedAt_=0; lastRlAt_=now;
    rlDurationMs_=0; rlDistanceBefore_=0;
    rlState_=0; rlAction_=0;
    for(uint8_t i=0;i<3;++i) rlDistanceHistory_[i]=0;
    rlHistoryCount_=0;
  }
  void applyLed(){
    switch(mode_){
      case OperatingMode::Manual: led_.setBlue(); break;
      case OperatingMode::Autonomous: led_.setGreen(); break;
      case OperatingMode::ReinforcementLearning: led_.setMagenta(); break;
    }
  }

  MotorDriver& motors_;
  DistanceSensor& sensor_;
  DualServoSystem& servos_;
  StatusLED& led_;
  SoundManager& sound_;
  QuantizedMLP ai_;
  QuantizedRL rlAgent_;

  OperatingMode mode_ = OperatingMode::Manual;
  DriveCommand manualCmd_ = DriveCommand::Stop;
  bool safetyStop_=false, emergencyLatched_=false;
  uint32_t lastManualAt_=0;

  AutoPhase autoPhase_ = AutoPhase::Cruise;
  uint32_t autoStartedAt_=0;
  uint16_t rightDistance_=0, leftDistance_=0, turnDurationMs_=0;

  bool rlActive_=false;
  uint32_t rlStartedAt_=0, lastRlAt_=0;
  uint16_t rlDurationMs_=0, rlDistanceBefore_=0;
  uint8_t rlState_=0, rlAction_=0;
  uint16_t rlDistanceHistory_[3] = {0,0,0};
  uint8_t  rlHistoryCount_ = 0;

  bool dancing_=false;
  uint8_t danceStep_=0;
  uint32_t danceStartedAt_=0, danceAt_=0;
};

#include "captive_portal.h"

MotorDriver motors(Config::MOTOR_IN1_PIN, Config::MOTOR_IN2_PIN,
                   Config::MOTOR_IN3_PIN, Config::MOTOR_IN4_PIN,
                   Config::MOTOR_ENA_PIN, Config::MOTOR_ENB_PIN);
DistanceSensor sensor(Config::ULTRASONIC_TRIG_PIN, Config::ULTRASONIC_ECHO_PIN);
DualServoSystem servos(Config::SERVO_SCAN_PIN, Config::SERVO_BALL_PIN);
StatusLED led(Config::LED_RED_PIN, Config::LED_GREEN_PIN, Config::LED_BLUE_PIN);
SoundManager sound(Config::BUZZER_PIN);
CarNavigator nav(motors, sensor, servos, led, sound);
PortalServer portal(nav);

void setup(){
  Serial.begin(115200);
  WiFi.persistent(false);

  const esp_task_wdt_config_t watchdogConfig = {
      .timeout_ms = 10000,
      .idle_core_mask = (1U << portNUM_PROCESSORS) - 1U,
      .trigger_panic = true};
  esp_err_t wdtResult = esp_task_wdt_init(&watchdogConfig);
  if(wdtResult==ESP_ERR_INVALID_STATE) esp_task_wdt_reconfigure(&watchdogConfig);
  else if(wdtResult!=ESP_OK) Serial.printf("Watchdog init failed: %d\n", wdtResult);
  esp_task_wdt_add(NULL);

  pinMode(Config::BATTERY_PIN, INPUT);
  analogReadResolution(12);

  motors.begin(); sensor.begin(); servos.begin(); led.begin(); sound.begin();
  randomSeed(esp_random());
  nav.begin(millis());
  portal.begin();
}

void loop(){
  esp_task_wdt_reset();
  uint32_t now = millis();
  sensor.update(now);
  sound.update(now);
  portal.update();
  nav.update(now);
  delay(5);
}