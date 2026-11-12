#ifndef __MODULE_TRUED_PROTO_H_
#define __MODULE_TRUED_PROTO_H_

// Furious True-D "LINK" bay mode: the True-D listens on one bay pin at 9600 8N1
// (and never answers).  Channel command:
//
//   FD 02 02 05 41 EE 00 <ch> <crc hi> <crc lo>
//
// ch 0..47 = A1-8, B1-8, E1-8, F1-8, R1-8, L1-8 (module_standard_channel_name()
// order), CRC-16/XMODEM (poly 0x1021, init 0) over the first 8 bytes, high byte
// first.  Plain C, also built by the host-side test (utilities/module_bay_test).

#include <stddef.h>
#include <stdint.h>

#define TRUED_BAUD         9600
#define TRUED_CMD_LEN      10
#define TRUED_NUM_CHANNELS 48

uint16_t trued_crc16_xmodem(const uint8_t *buf, size_t len);

// channel: 0..TRUED_NUM_CHANNELS-1
void trued_build_channel_cmd(uint8_t channel, uint8_t out[TRUED_CMD_LEN]);

#endif
