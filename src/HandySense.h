// Macro สำหรับ map relayId (0-based) เป็น switchId (1-based)
#ifndef RELAY_ID_TO_SWITCH_ID
#define RELAY_ID_TO_SWITCH_ID(relayId) ((relayId) + 1)
#endif

#define SWITCH_POLL_INTERVAL 5000  // 5 วินาที

#pragma once

void HandySense_init() ;
void HandySense_loop() ;

void HandySense_updateTimeInTimer(uint8_t relay, uint8_t timer, bool isTimeOn, uint16_t time) ;
void HandySense_updateDayEnableInTimer(uint8_t relay, uint8_t timer, uint8_t day, bool enable) ;
void HandySense_updateDisableTimer(uint8_t relay, uint8_t timer) ;
void HandySense_setTempMin(uint8_t relay, int value) ;
void HandySense_setTempMax(uint8_t relay, int value) ;
void HandySense_setSoilMin(uint8_t relay, int value) ;
void HandySense_setSoilMax(uint8_t relay, int value) ;
