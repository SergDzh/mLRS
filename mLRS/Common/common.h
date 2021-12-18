//*******************************************************
// Copyright (c) OlliW, OlliW42, www.olliw.eu
// MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
//*******************************************************
// common
//*******************************************************
#ifndef COMMON_H
#define COMMON_H
#pragma once


#include "common_conf.h"
#include "sx1280_driver.h"
#include "lq_counter.h"
#include "fhss.h"
#include "frame_types.h"


//-------------------------------------------------------
// SysTask
//-------------------------------------------------------

volatile uint8_t doSysTask = 0;


void HAL_IncTick(void)
{
    uwTick += uwTickFreq;
    doSysTask = 1;
}


uint32_t millis(void)
{
    return uwTick;
}


//-------------------------------------------------------
// frames
//-------------------------------------------------------

STATIC_ASSERT(sizeof(tTxFrame) == FRAME_TX_RX_LEN, "Frame len missmatch TxFrame")
STATIC_ASSERT(sizeof(tRxFrame) == FRAME_TX_RX_LEN, "Frame len missmatch RxFrame")


void pack_tx_frame(tTxFrame* frame, tFrameStats* frame_stats, tRcData* rc, uint8_t* payload, uint8_t payload_len)
{
static uint8_t seq_no = 0;
uint16_t crc;

    if (payload_len > FRAME_TX_PAYLOAD_LEN) payload_len = FRAME_TX_PAYLOAD_LEN; // should never occur, but play it safe

    memset(frame, 0, sizeof(tTxFrame));

    // generate header
    frame->sync_word = FRAME_SYNCWORD;
    frame->status.seq_no = seq_no;
    seq_no++;
    frame->status.ack = 0;
    frame->status.frame_type = FRAME_TYPE_TX;
    frame->status.rssi_u8 = -(frame_stats->rssi);
    frame->status.LQ = frame_stats->LQ;
    frame->status.payload_len = payload_len;

    // pack rc data
    // rcData: 0 .. 1024 .. 2047, 11 bits
    frame->rc1.ch0  = rc->ch[0]; // 0 .. 1024 .. 2047, 11 bits
    frame->rc1.ch1  = rc->ch[1];
    frame->rc1.ch2  = rc->ch[2];
    frame->rc1.ch3  = rc->ch[3];
    for (uint8_t i = 0; i < FRAME_TX_RCDATA2_LEN; i++) {
      frame->rc2.ch[i] = (rc->ch[4+i] / 8); // 0 .. 128 .. 255, 8 bits
    }
    frame->rc1.ch14 = (rc->ch[14] >= 1536) ? 1 : 0; // 0 ... 1
    frame->rc1.ch15 = (rc->ch[15] >= 1536) ? 1 : 0;
    frame->rc1.ch16 = (rc->ch[16] >= 1536) ? 1 : 0;
    frame->rc1.ch17 = (rc->ch[17] >= 1536) ? 1 : 0;

    // pack the payload
    for (uint8_t i = 0; i < payload_len; i++) {
      frame->payload[i] = payload[i];
    }

    // finalize, crc
    fmav_crc_init(&crc);
    fmav_crc_accumulate_buf(&crc, (uint8_t*)frame, FRAME_HEADER_LEN + FRAME_TX_RCDATA1_LEN);
    frame->crc1 = (uint8_t)crc;

    fmav_crc_accumulate_buf(&crc, (uint8_t*)&(frame->crc1), FRAME_TX_RCDATA1_LEN + 1);
    frame->crc2 = (uint8_t)crc;

// ???    fmav_crc_accumulate_buf(&crc, (uint8_t*)&(frame->crc2), FRAME_TX_PAYLOAD_LEN + 1);
    fmav_crc_init(&crc);
    fmav_crc_accumulate_buf(&crc, (uint8_t*)frame, FRAME_TX_RX_LEN - 2);
    frame->crc = crc;
}


// returns 0 if OK !!
uint16_t check_tx_frame(tTxFrame* frame)
{
uint16_t err_flags = 0;
uint16_t crc;

  if (frame->sync_word != FRAME_SYNCWORD) err_flags |= 0x0001;
  if (frame->status.frame_type != FRAME_TYPE_TX) err_flags |= 0x0002;
  if (frame->status.payload_len > FRAME_TX_PAYLOAD_LEN) err_flags |= 0x0004;

  if (err_flags) return err_flags; // avoid the expensive crc calculation

  fmav_crc_init(&crc);
  fmav_crc_accumulate_buf(&crc, (uint8_t*)frame, FRAME_HEADER_LEN + FRAME_TX_RCDATA1_LEN);
  if ((uint8_t)crc != frame->crc1) err_flags |= 0x0008;

  fmav_crc_init(&crc);
  fmav_crc_accumulate_buf(&crc, (uint8_t*)frame, FRAME_TX_RX_LEN - 2);
  if (crc != frame->crc) err_flags |= 0x0010;

  return err_flags;
}


void rcdata_ch0to3_from_txframe(tRcData* rc, tTxFrame* frame)
{
  rc->ch[0] = frame->rc1.ch0;
  rc->ch[1] = frame->rc1.ch1;
  rc->ch[2] = frame->rc1.ch2;
  rc->ch[3] = frame->rc1.ch3;
}


void rcdata_from_txframe(tRcData* rc, tTxFrame* frame)
{
  rc->ch[0] = frame->rc1.ch0;
  rc->ch[1] = frame->rc1.ch1;
  rc->ch[2] = frame->rc1.ch2;
  rc->ch[3] = frame->rc1.ch3;
  for (uint8_t i = 0; i < FRAME_TX_RCDATA2_LEN; i++) {
    rc->ch[4+i] = frame->rc2.ch[i] * 8;
  }
  rc->ch[14] = (frame->rc1.ch14) ? 2047 : 0;
  rc->ch[15] = (frame->rc1.ch15) ? 2047 : 0;
  rc->ch[16] = (frame->rc1.ch16) ? 2047 : 0;
  rc->ch[17] = (frame->rc1.ch17) ? 2047 : 0;
}


void pack_rx_frame(tRxFrame* frame, tFrameStats* frame_stats, uint8_t* payload, uint8_t payload_len)
{
uint16_t crc;

    if (payload_len > FRAME_RX_PAYLOAD_LEN) payload_len = FRAME_RX_PAYLOAD_LEN; // should never occur, but play it safe

    memset(frame, 0, sizeof(tRxFrame));

    frame->sync_word = FRAME_SYNCWORD;
    frame->status.seq_no = frame_stats->seq_no;
    frame->status.ack = 0;
    frame->status.frame_type = FRAME_TYPE_RX;
    frame->status.rssi_u8 = -(frame_stats->rssi);
    frame->status.LQ = frame_stats->LQ;
    frame->status.payload_len = payload_len;

    for (uint8_t i = 0; i < payload_len; i++) {
      frame->payload[i] = payload[i];
    }

    fmav_crc_init(&crc);
    fmav_crc_accumulate_buf(&crc, (uint8_t*)frame, FRAME_TX_RX_LEN - 2);
    frame->crc = crc;
}


// returns 0 if OK !!
uint16_t check_rx_frame(tRxFrame* frame)
{
uint16_t err_flags = 0;
uint16_t crc;

  if (frame->sync_word != FRAME_SYNCWORD) err_flags |= 0x0001;
  if (frame->status.frame_type != FRAME_TYPE_RX) err_flags |= 0x0002;
  if (frame->status.payload_len > FRAME_RX_PAYLOAD_LEN) err_flags |= 0x0004;

  if (err_flags) return err_flags; // avoid the expensive crc calculation

  fmav_crc_init(&crc);
  fmav_crc_accumulate_buf(&crc, (uint8_t*)frame, FRAME_TX_RX_LEN - 2);
  if (crc != frame->crc) err_flags |= 0x0008;

  return err_flags;
}


//-------------------------------------------------------
// Stats
//-------------------------------------------------------

class Stats {
  public:
    uint32_t frames_transmitted; // this is quite useless, seems to be always 100%
    uint32_t frames_received;
    uint32_t valid_frames_received; // received frames which also pass check_rx_frame()

    uint8_t LQ_transmitted; // this is quite useless, seems to be always 100%
    uint8_t LQ_received;
    uint8_t LQ_valid_received;

    int8_t last_rssi; // note: is negative!
    int8_t last_snr; // note: can be negative!
    uint8_t LQ;

    uint32_t valid_crc1_frames_received; // received frames which passed check_rx_frame() up to crc1, but not crc
    uint8_t LQ_valid_crc1_received;

    // statistics received from the other end
    uint8_t received_seq_no;
    int8_t received_rssi; // note: is negative!
    uint8_t received_LQ;

    void Init(void)
    {
      frames_transmitted = frames_received = valid_frames_received = 0;
      LQ_transmitted = LQ_received = LQ_valid_received = LQ = 0; //UINT8_MAX;
      last_rssi = last_snr = INT8_MAX;
      received_seq_no = 0;
      received_rssi = INT8_MAX;
      received_LQ = 0; //UINT8_MAX;

      // only tx frames
      valid_crc1_frames_received = 0;
      LQ_valid_crc1_received = 0;
    }

    void Update(void)
    {
      LQ_transmitted = (frames_transmitted * 100 * FRAME_RATE_MS) / 1000;
      LQ_received = (frames_received * 100 * FRAME_RATE_MS) / 1000;
      LQ_valid_received = (valid_frames_received * 100 * FRAME_RATE_MS) / 1000;

      LQ = LQ_transmitted;
      if (LQ_received < LQ) LQ = LQ_received;
#ifdef DEVICE_IS_TRANSMITTER
      if (LQ_valid_received < LQ) LQ = LQ_valid_received;
#endif
#ifdef DEVICE_IS_RECEIVER
      LQ_valid_crc1_received = (valid_crc1_frames_received * 100 * FRAME_RATE_MS) / 1000;
      valid_crc1_frames_received = 0;

      if (LQ_valid_crc1_received < LQ) LQ = LQ_valid_crc1_received; //TODO: we may want to choose a better algorithm
      if (LQ_valid_received < LQ) LQ = LQ_valid_received;
#endif

      frames_transmitted = 0;
      frames_received = 0;
      valid_frames_received = 0;
    }

    void Reset(void)
    {
      Init();
    }
};


//-------------------------------------------------------
// Generic Serial Class
//-------------------------------------------------------

class tSerialBase
{
  public:
    void Init(void) {};
    virtual void putc(char c) {}
    void putbuf(void* buf, uint16_t len) { for (uint16_t i = 0; i < len; i++) putc(((char*)buf)[i]); }
    bool available(void) { return 0; }
    char getc(void) { return '\0'; }
};

// this is the serial port
// we have setup the hals such that it is always uartb
class tSerialPort : public tSerialBase
{
  public:
    void Init(void) { uartb_init(); }
    void putc(char c) override { uartb_putc(c); }
    bool available(void) { return uartb_rx_available(); }
    char getc(void) { return uartb_getc(); }
};


//-------------------------------------------------------
// common variables
//-------------------------------------------------------

tSerialPort serial;

tRcData rcData;
tTxFrame txFrame;
tRxFrame rxFrame;

SxDriver sx;
Stats stats;

FhssBase fhss;



#endif // COMMON_H
