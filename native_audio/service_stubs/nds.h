// Host-only register/FIFO model for the production ARM7 command service.
#pragma once
#include <cstdint>
inline uint32_t testSound[16]{}, testAddress[16]{}, testLoop[16]{}, testLength[16]{};
inline uint16_t testTimer[16]{}, testCounter[4]{};
inline unsigned testAccess[16]{};
template<class T> inline T &testRegister(T (&registers)[16], unsigned channel) {
    ++testAccess[channel];
    return registers[channel];
}
inline uint32_t REG_SNDCAP0CNT{}, REG_SNDCAP1CNT{}, REG_SNDCAP0DAD{}, REG_SNDCAP1DAD{};
inline uint32_t REG_SNDCAP0LEN{}, REG_SNDCAP1LEN{}, testAck{};
inline bool testSendOK = true, testAutoAck = true, testAckPending = false;
inline uint32_t testReply = 0, testSfxValue = 0;
inline unsigned testSends[2]{}, testWaits = 0;
inline void *testSharedAddress = nullptr;
inline const void *testLastFlush = nullptr;
inline unsigned testFlushedBytes = 0;
#define REG_SOUNDXCNT(n) testRegister(testSound, n)
#define REG_SOUNDXSAD(n) testRegister(testAddress, n)
#define REG_SOUNDXPNT(n) testRegister(testLoop, n)
#define REG_SOUNDXLEN(n) testRegister(testLength, n)
#define REG_SOUNDXTMR(n) testRegister(testTimer, n)
#define TIMER_DATA(n) testCounter[n]
#define SOUNDXCNT_ENABLE (1u << 31)
#define SOUNDXCNT_FORMAT_8BIT 0
#define SOUNDXCNT_FORMAT_16BIT (1u << 29)
#define SOUNDXCNT_REPEAT (1u << 27)
#define SOUNDXCNT_ONE_SHOT (2u << 27)
#define SOUNDXCNT_PAN(n) ((uint32_t(n) & 127u) << 16)
#define SNDCAPCNT_START_BUSY (1u << 7)
#define SNDCAPCNT_ONESHOT (1u << 2)
#define SND0CAPCNT_SOURCE_LEFT_MIXER 0
#define SND1CAPCNT_SOURCE_RIGHT_MIXER 0
enum { FIFO_USER_01 = 0, FIFO_USER_02 = 1, IRQ_TIMER1 = 1, ClockDivider_64 = 64 };
inline void irqDisable(unsigned) {}
inline void irqEnable(unsigned) {}
inline bool fifoSendValue32(unsigned fifo, uint32_t value) {
    ++testSends[fifo];
    if (!testSendOK) return false;
    if (fifo == FIFO_USER_02) testSfxValue = value;
    else { testAck = value; testAckPending = testAutoAck; }
    return true;
}
inline bool fifoSendAddress(unsigned, void *address) { testSharedAddress = address; return testSendOK; }
inline bool fifoCheckValue32(unsigned) { return testAckPending; }
inline uint32_t fifoGetValue32(unsigned) { testAckPending = false; return testReply; }
inline void DC_FlushRange(const void *address, unsigned bytes) { testLastFlush = address; testFlushedBytes = bytes; }
inline void *memUncached(void *address) { return address; }
inline void swiWaitForVBlank() { ++testWaits; }
inline int enterCriticalSection() { return 1; }
inline void leaveCriticalSection(int) {}
inline void fifoSetAddressHandler(unsigned, void (*)(void *, void *), void *) {}
inline void fifoSetValue32Handler(unsigned, void (*)(uint32_t, void *), void *) {}
inline void systemCounterSetup() {}
inline void timerStart(unsigned, unsigned, uint16_t, void (*)()) {}
inline void timerStop(unsigned) {}
