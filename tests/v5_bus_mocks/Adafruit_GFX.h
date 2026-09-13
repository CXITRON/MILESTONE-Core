#pragma once
#include <algorithm>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
using std::min; using std::max;
#define constrain(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
constexpr int OUTPUT=1, LOW=0, HIGH=1;
namespace BusMock { static int pins[64]{}; static uint32_t now=0; static bool noMemory=false; }
inline uint32_t micros(){return BusMock::now++;}
inline void pinMode(int,int){}
inline void digitalWrite(int pin,int value){BusMock::pins[pin]=value;}
inline void delay(unsigned ms){BusMock::now+=ms*1000;}
class Adafruit_GFX {
public:
 Adafruit_GFX(int w,int h):w_(w),h_(h){} virtual ~Adafruit_GFX(){}
 int width()const{return w_;} int height()const{return h_;}
 virtual void drawPixel(int16_t,int16_t,uint16_t)=0;
 virtual void startWrite(){} virtual void endWrite(){}
 virtual void writePixel(int16_t,int16_t,uint16_t){}
 virtual void writeFillRect(int16_t,int16_t,int16_t,int16_t,uint16_t){}
 virtual void writeFastHLine(int16_t,int16_t,int16_t,uint16_t){}
 virtual void writeFastVLine(int16_t,int16_t,int16_t,uint16_t){}
private:int w_,h_;
};
