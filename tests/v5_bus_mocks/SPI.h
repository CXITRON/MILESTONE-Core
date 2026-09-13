#pragma once
#include <Adafruit_GFX.h>
#include <cassert>
constexpr int MSBFIRST=1,SPI_MODE0=0;
struct SPISettings { uint32_t hz; SPISettings(uint32_t h,int,int):hz(h){} };
class SPIClass {
public:
 bool active=false; uint32_t hz=0, tftWrites=0, largestWrite=0;
 void beginTransaction(SPISettings s){assert(!active);active=true;hz=s.hz;}
 void endTransaction(){assert(active);assert(BusMock::pins[47]==HIGH);active=false;}
 uint8_t transfer(uint8_t){check();return 0;}
 void writeBytes(const uint8_t*,size_t n){check(); if(BusMock::pins[47]==LOW){++tftWrites;largestWrite=max(largestWrite,uint32_t(n));} BusMock::now+=n;}
 void check(){assert(active);assert(BusMock::pins[47]||BusMock::pins[10]);if(BusMock::pins[47]==LOW)assert(hz==20000000);++BusMock::now;}
};
