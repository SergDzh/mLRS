//*******************************************************
// Copyright (c) MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
// OlliW @ www.olliw.eu
//*******************************************************
// mLRS RX
/********************************************************

v0.0.00:
*/

#define DBG_MAIN(x)
#define DBG_MAIN_SLIM(x)
#define DEBUG_ENABLED


// we set the priorities here to have an overview
#define CLOCK_IRQ_PRIORITY          10
#define UARTB_IRQ_PRIORITY          11 // serial
#define UARTC_IRQ_PRIORITY          11 // debug
#define UART_IRQ_PRIORITY           12 // SBus out pin
#define SX_DIO_EXTI_IRQ_PRIORITY    13
#define SX2_DIO_EXTI_IRQ_PRIORITY   13

#include "..\Common\common_conf.h"
#include "..\Common\common_types.h"
#include "..\Common\hal\glue.h"
#include "..\modules\stm32ll-lib\src\stdstm32.h"
#include "..\modules\stm32ll-lib\src\stdstm32-peripherals.h"
#include "..\Common\sx-drivers\sx12xx.h"
#include "..\Common\hal\hal.h"
#include "..\modules\stm32ll-lib\src\stdstm32-delay.h" // these are dependent on hal
#include "..\modules\stm32ll-lib\src\stdstm32-spi.h"
#ifdef DEVICE_HAS_DIVERSITY
#include "..\modules\stm32ll-lib\src\stdstm32-spib.h"
#endif
#ifndef DEVICE_HAS_NO_SERIAL
#include "..\modules\stm32ll-lib\src\stdstm32-uartb.h"
#endif
#ifndef DEVICE_HAS_NO_DEBUG
#include "..\modules\stm32ll-lib\src\stdstm32-uartc.h"
#endif
#include "..\modules\stm32ll-lib\src\stdstm32-uart.h"
#define FASTMAVLINK_IGNORE_WADDRESSOFPACKEDMEMBER
#include "..\Common\mavlink\out\mlrs\mlrs.h"
#include "..\Common\fhss.h"
#include "..\Common\setup.h"
#include "..\Common\common.h"
#include "..\Common\micros.h"
//#include "..\Common\test.h" // un-comment if you want to compile for board test

#include "clock.h"
#include "out.h"
#include "rxstats.h"


ClockBase clock;


class Out : public OutBase
{
public:
  void Init(void)
  {
    OutBase::Init();
    out_init_gpio();
    uart_init_isroff();
  }

  bool config_sbus(bool enable_flag) override
  {
    if (enable_flag) {
      uart_setprotocol(100000, XUART_PARITY_EVEN, UART_STOPBIT_2);
      out_set_inverted();
    }
    return true;
  }

  bool config_crsf(bool enable_flag) override
  {
    if (enable_flag) {
      uart_setprotocol(416666, XUART_PARITY_EVEN, UART_STOPBIT_1);
      out_set_normal();
    }
    return true;
  }

  void putc(char c) override { uart_putc(c); }

  void SendLinkStatistics(void);
};

Out out;


void init(void)
{
  leds_init();

  delay_init();
  micros_init();

  serial.Init(); //uartb_setprotocol(SETUP_RX_SERIAL_BAUDRATE, XUART_PARITY_NO, UART_STOPBIT_1);
  out.Init();

  dbg.Init();

  setup_init(); // clock needs Config

  clock.Init();

  sx.Init();
  sx2.Init();
}


//-------------------------------------------------------
// Statistics for Receiver
//-------------------------------------------------------

static inline bool connected(void);

class RxStats : public RxStatsBase
{
  bool is_connected(void) override { return connected(); }
};

RxStats rxstats;


void Out::SendLinkStatistics(void)
{
  tOutLinkStats lstats = {
    .receiver_rssi1 = (stats.last_rx_antenna == ANTENNA_1) ? stats.last_rx_rssi1 : (int8_t)RSSI_MIN,
    .receiver_rssi2 = (stats.last_rx_antenna == ANTENNA_2) ? stats.last_rx_rssi2 : (int8_t)RSSI_MIN,
    .receiver_LQ = rxstats.GetLQ(),
    .receiver_snr = (stats.last_rx_antenna == ANTENNA_1) ? stats.last_rx_snr1 : stats.last_rx_snr2,
    .receiver_antenna = stats.last_rx_antenna,
    .receiver_transmit_antenna = stats.last_tx_antenna,
    .receiver_power = 0, // TODO
    .transmitter_rssi = stats.received_rssi,
    .transmitter_LQ = stats.received_LQ,
    .transmitter_snr = 0,
    .transmitter_antenna = stats.received_antenna,
    .transmitter_transmit_antenna = stats.received_transmit_antenna,
  };
  OutBase::SendLinkStatistics(&lstats);
}


//-------------------------------------------------------
// mavlink
//-------------------------------------------------------

#include "mavlink_interface.h"


//-------------------------------------------------------
// SX1280
//-------------------------------------------------------

volatile uint16_t irq_status;
volatile uint16_t irq2_status;

IRQHANDLER(
void SX_DIO_EXTI_IRQHandler(void)
{
  LL_EXTI_ClearFlag_0_31(SX_DIO_EXTI_LINE_x);
  //LED_RED_TOGGLE;
  irq_status = sx.GetAndClearIrqStatus(SX12xx_IRQ_ALL);
  if (irq_status & SX12xx_IRQ_RX_DONE) {
    uint16_t sync_word;
    sx.ReadBuffer(0, (uint8_t*)&sync_word, 2); // rxStartBufferPointer is always 0, so no need for sx.GetRxBufferStatus()
    if (sync_word != Config.FrameSyncWord) irq_status = 0; // not for us, so ignore it
  }
})
#ifdef DEVICE_HAS_DIVERSITY
IRQHANDLER(
void SX2_DIO_EXTI_IRQHandler(void)
{
  LL_EXTI_ClearFlag_0_31(SX2_DIO_EXTI_LINE_x);
  //LED_RED_TOGGLE;
  irq2_status = sx2.GetAndClearIrqStatus(SX12xx_IRQ_ALL);
  if (irq2_status & SX12xx_IRQ_RX_DONE) {
    uint16_t sync_word;
    sx2.ReadBuffer(0, (uint8_t*)&sync_word, 2);
    if (sync_word != Config.FrameSyncWord) irq2_status = 0;
  }
})
#endif


typedef enum {
    CONNECT_STATE_LISTEN = 0,
    CONNECT_STATE_SYNC,
    CONNECT_STATE_CONNECTED,
} CONNECT_STATE_ENUM;

typedef enum {
    LINK_STATE_RECEIVE = 0,
    LINK_STATE_RECEIVE_WAIT,
    LINK_STATE_TRANSMIT,
    LINK_STATE_TRANSMIT_WAIT,
} LINK_STATE_ENUM;

#define IS_RECEIVE_STATE    (link_state == LINK_STATE_RECEIVE || link_state == LINK_STATE_RECEIVE_WAIT)

typedef enum {
    RX_STATUS_NONE = 0, // no frame received
    RX_STATUS_INVALID,
    RX_STATUS_CRC1_VALID,
    RX_STATUS_VALID,
} RX_STATUS_ENUM;

uint8_t link_rx1_status;
uint8_t link_rx2_status;


uint8_t payload[FRAME_RX_PAYLOAD_LEN] = {0};
uint8_t payload_len = 0;


void process_transmit_frame(uint8_t antenna, uint8_t ack)
{
  // read data from serial
  if (connected()) {
    memset(payload, 0, FRAME_TX_PAYLOAD_LEN);
    payload_len = 0;

    for (uint8_t i = 0; i < FRAME_RX_PAYLOAD_LEN; i++) {
      if (!serial.available()) break;
      payload[payload_len] = serial.getc();
      payload_len++;
    }

    stats.bytes_transmitted.Add(payload_len);
    stats.fresh_serial_data_transmitted.Inc();

  } else {
    serial.flush();
    memset(payload, 0, FRAME_TX_PAYLOAD_LEN);
    payload_len = 0;
  }

  stats.last_tx_antenna = antenna;

  tFrameStats frame_stats;
  frame_stats.seq_no = stats.transmit_seq_no;
  frame_stats.ack = ack;
  frame_stats.antenna = stats.last_rx_antenna;
  frame_stats.transmit_antenna = antenna;
  frame_stats.rssi = (antenna == ANTENNA_1) ? stats.last_rx_rssi1 : stats.last_rx_rssi2;
  frame_stats.LQ = rxstats.GetLQ();
  frame_stats.LQ_serial_data = rxstats.GetLQ_serial_data();

  pack_rx_frame(&rxFrame, &frame_stats, payload, payload_len);

  if (antenna == ANTENNA_1) {
    sx.SendFrame((uint8_t*)&rxFrame, FRAME_TX_RX_LEN, SEND_FRAME_TMO); // 10ms tmo
  } else {
    sx2.SendFrame((uint8_t*)&rxFrame, FRAME_TX_RX_LEN, SEND_FRAME_TMO); // 10ms tmo
  }
}


void process_received_frame(bool do_payload, tTxFrame* frame)
{
  stats.received_antenna = frame->status.antenna;
  stats.received_transmit_antenna = frame->status.transmit_antenna;
  stats.received_rssi = rssi_i8_from_u7(frame->status.rssi_u7);
  stats.received_LQ = frame->status.LQ;
  stats.received_LQ_serial_data = frame->status.LQ_serial_data;

  // copy rc data
  if (!do_payload) {
    // copy only channels 1-4, and jump out
    rcdata_rc1_from_txframe(&rcData, frame);
    return;
  }

  rcdata_from_txframe(&rcData, frame);

  // output data on serial, but only if connected
  if (connected()) {
    for (uint8_t i = 0; i < frame->status.payload_len; i++) {
      uint8_t c = frame->payload[i];
      serial.putc(c); // send to serial
      if (Setup.Rx.SendRadioStatus) {
        uint8_t res = fmav_parse_to_frame_buf(&f_result, f_buf, &f_status, c);
        if (res == FASTMAVLINK_PARSE_RESULT_OK && inject_radio_status) { // we have a complete mavlink frame
          inject_radio_status = false;
          if (Setup.Rx.SendRadioStatus == SEND_RADIO_STATUS_ON) send_radio_status();
          if (Setup.Rx.SendRadioStatus == SEND_RADIO_STATUS_V2_ON) send_radio_status_v2();
          LED_RED_TOGGLE;	// indicate we send the radio status
        }
      }
    }

    stats.bytes_received.Add(frame->status.payload_len);
    stats.fresh_serial_data_received.Inc();
  }
}


void handle_receive(uint8_t antenna)
{
uint8_t rx_status;
tTxFrame* frame;

  if (antenna == ANTENNA_1) {
    rx_status = link_rx1_status;
    frame = &txFrame;
  } else {
    rx_status = link_rx2_status;
    frame = &txFrame2;
  }

  if (rx_status != RX_STATUS_INVALID) {

    bool do_payload = (rx_status == RX_STATUS_VALID);

    process_received_frame(do_payload, frame);

    rxstats.doValidCrc1FrameReceived();
    if (rx_status == RX_STATUS_VALID) rxstats.doValidFrameReceived();

    stats.received_seq_no_last = frame->status.seq_no;
    stats.received_ack_last = frame->status.ack;

  } else {
    stats.received_seq_no_last = UINT8_MAX;
    stats.received_ack_last = 0;
  }

  // we set it for all received frames
  stats.last_rx_antenna = antenna;

  // we count all received frames
  rxstats.doFrameReceived();
}


void do_transmit(uint8_t antenna) // we send a RX frame to transmitter
{
  uint8_t ack = 1;

  stats.transmit_seq_no++;

  process_transmit_frame(antenna, ack);
}


uint8_t do_receive(uint8_t antenna, bool do_clock_reset) // we receive a TX frame from receiver
{
uint8_t res;
uint8_t rx_status = RX_STATUS_INVALID; // this also signals that a frame was received

  // we don't need to read sx.GetRxBufferStatus(), but hey
  // we could save 2 byte's time by not reading sync_word again, but hey
  if (antenna == ANTENNA_1) {
    sx.ReadFrame((uint8_t*)&txFrame, FRAME_TX_RX_LEN);
    res = check_tx_frame(&txFrame);
  } else {
    sx2.ReadFrame((uint8_t*)&txFrame2, FRAME_TX_RX_LEN);
    res = check_tx_frame(&txFrame2);
  }

  if (res) {
    DBG_MAIN(dbg.puts("fail ");dbg.putc('\n');)
dbg.puts("fail ");dbg.puts(u8toHEX_s(res)); dbg.puts(" ");
  }

  if (res == CHECK_ERROR_SYNCWORD) return false; // must not happen !

  if (res == CHECK_OK || res == CHECK_ERROR_CRC) {

    if (do_clock_reset) clock.Reset();

    rx_status = (res == CHECK_OK) ? RX_STATUS_VALID : RX_STATUS_CRC1_VALID;
  }

  // we want to have it even if it's a bad packet
  if (antenna == ANTENNA_1) {
    sx.GetPacketStatus(&stats.last_rx_rssi1, &stats.last_rx_snr1);
  } else {
    sx2.GetPacketStatus(&stats.last_rx_rssi2, &stats.last_rx_snr2);
  }

  return rx_status;
}


//##############################################################################################################
//*******************************************************
// MAIN routine
//*******************************************************

uint16_t led_blink;
uint16_t tick_1hz;

uint8_t link_state;
uint8_t connect_state;
uint16_t connect_tmo_cnt;
uint8_t connect_sync_cnt;
uint8_t connect_listen_cnt;
bool connect_occured_once;

uint8_t doPostReceive2_cnt;
bool doPostReceive2;
bool frame_missed;


static inline bool connected(void)
{
  return (connect_state == CONNECT_STATE_CONNECTED);
}


int main_main(void)
{
#ifdef BOARD_TEST_H
  main_test();
#endif
  init();

  DBG_MAIN(dbg.puts("\n\n\nHello\n\n");)

  // startup sign of life
  LED_RED_OFF;
  for (uint8_t i = 0; i < 7; i++) { LED_RED_TOGGLE; delay_ms(50); }

  // start up sx
  if (!sx.isOk()) {
    while (1) { LED_RED_TOGGLE; delay_ms(25); } // fail!
  }
  if (!sx2.isOk()) {
    while (1) { LED_GREEN_TOGGLE; delay_ms(25); } // fail!
  }
  IF_ANTENNA1(sx.StartUp());
  IF_ANTENNA2(sx2.StartUp());
  fhss.Init(Config.FhssNum, Config.FhssSeed);
  fhss.StartRx();
  fhss.HopToConnect();

  sx.SetRfFrequency(fhss.GetCurrFreq());
  sx2.SetRfFrequency(fhss.GetCurrFreq());

//  for (uint8_t i = 0; i < fhss.Cnt(); i++) {
//    dbg.puts("c = ");dbg.puts(u8toBCD_s(fhss.ch_list[i]));dbg.puts(" f = ");dbg.puts(u32toBCD_s(fhss.fhss_list[i]));dbg.puts("\n"); delay_ms(50);
//  }

  link_state = LINK_STATE_RECEIVE;
  connect_state = CONNECT_STATE_LISTEN;
  connect_tmo_cnt = 0;
  connect_listen_cnt = 0;
  connect_sync_cnt = 0;
  connect_occured_once = false;
  link_rx1_status = RX_STATUS_NONE;
  link_rx2_status = RX_STATUS_NONE;

  rxstats.Init(Config.LQAveragingPeriod);

  out.Configure(Setup.Rx.OutMode);

  led_blink = 0;
  tick_1hz = 0;
  doPostReceive2_cnt = 0;
  doPostReceive2 = false;
  frame_missed = false;
  doSysTask = 0; // helps in avoiding too short first loop
  while (1) {

    if (doSysTask) {
      doSysTask = 0;

      if (connect_tmo_cnt) {
        connect_tmo_cnt--;
      }

      DECc(tick_1hz, SYSTICK_DELAY_MS(1000));
      if (connected()) {
        DECc(led_blink, SYSTICK_DELAY_MS(500));
      } else
      if (IS_RECEIVE_STATE) {
        DECc(led_blink, SYSTICK_DELAY_MS(200));
      } else {
        DECc(led_blink, SYSTICK_DELAY_MS(50));
      }

      if (!led_blink) {
        if (connected()) LED_GREEN_TOGGLE; else LED_RED_TOGGLE;
      }
      if (connected()) { LED_RED_OFF; } else { LED_GREEN_OFF; }

      if (!tick_1hz) {
        rxstats.Update1Hz();
        if (connected()) inject_radio_status = true;

        dbg.puts("\nRX: ");
        dbg.puts(u8toBCD_s(rxstats.GetLQ())); dbg.putc(',');
        dbg.puts(u8toBCD_s(rxstats.GetLQ_serial_data()));
        dbg.puts(" (");
        dbg.puts(u8toBCD_s(stats.frames_received.GetLQ())); dbg.putc(',');
        dbg.puts(u8toBCD_s(stats.valid_crc1_received.GetLQ())); dbg.putc(',');
        dbg.puts(u8toBCD_s(stats.valid_frames_received.GetLQ()));
        dbg.puts("),");
        dbg.puts(u8toBCD_s(stats.received_LQ)); dbg.puts(", ");

        dbg.puts(s8toBCD_s(stats.last_rx_rssi1)); dbg.putc(',');
        dbg.puts(s8toBCD_s(stats.received_rssi)); dbg.puts(", ");
        dbg.puts(s8toBCD_s(stats.last_rx_snr1)); dbg.puts("; ");

        dbg.puts(u16toBCD_s(stats.bytes_transmitted.GetBytesPerSec())); dbg.puts(", ");
        dbg.puts(u16toBCD_s(stats.bytes_received.GetBytesPerSec())); dbg.puts("; ");
      }
    }

    //-- SX handling

    switch (link_state) {
    case LINK_STATE_RECEIVE: {
      if (connect_state == CONNECT_STATE_LISTEN) {
        fhss.HopToConnect();
      } else {
        fhss.HopToNext();
      }
      sx.SetRfFrequency(fhss.GetCurrFreq());
      sx2.SetRfFrequency(fhss.GetCurrFreq());
      IF_ANTENNA1(sx.SetToRx(0)); // single without tmo
      IF_ANTENNA2(sx2.SetToRx(0));
      link_state = LINK_STATE_RECEIVE_WAIT;
      link_rx1_status = RX_STATUS_NONE;
      link_rx2_status = RX_STATUS_NONE;
      irq_status = 0;
      irq2_status = 0;
      DBG_MAIN_SLIM(dbg.puts("\n>");)
      }break;

    case LINK_STATE_TRANSMIT: {
      // TODO: transmit antenna diversity
      do_transmit((USE_ANTENNA1) ? ANTENNA_1 : ANTENNA_2);
      link_state = LINK_STATE_TRANSMIT_WAIT;
      irq_status = 0; // important, in low connection condition, RxDone isr could trigger
      irq2_status = 0;
      }break;

    }//end of switch(link_state)

IF_ANTENNA1(
    if (irq_status) {
      if (link_state == LINK_STATE_TRANSMIT_WAIT) {
        if (irq_status & SX12xx_IRQ_TX_DONE) {
          irq_status = 0;
          link_state = LINK_STATE_RECEIVE;
          DBG_MAIN_SLIM(dbg.puts("<");)
        }
      } else
      if (link_state == LINK_STATE_RECEIVE_WAIT) {
        if (irq_status & SX12xx_IRQ_RX_DONE) {
          irq_status = 0;
          bool do_clock_reset = (link_rx2_status == RX_STATUS_NONE);
          link_rx1_status = do_receive(ANTENNA_1, do_clock_reset);
          DBG_MAIN_SLIM(dbg.puts("!");)
        }
      }

      if (irq_status & SX12xx_IRQ_RX_DONE) { // R, T, TW
        LED_GREEN_OFF;
        while (1) { LED_RED_ON; delay_ms(25); LED_RED_OFF; delay_ms(25); }
      }
      if (irq_status & SX12xx_IRQ_TX_DONE) {
        LED_RED_OFF;
        while (1) { LED_GREEN_ON; delay_ms(25); LED_GREEN_OFF; delay_ms(25); }
      }
      if (irq_status & SX12xx_IRQ_TIMEOUT) {
        while (1) { LED_RED_ON; LED_GREEN_ON; delay_ms(50); LED_RED_OFF; LED_GREEN_OFF; delay_ms(50); }
      }
    }//end of if(irq_status)
);
IF_ANTENNA2(
    if (irq2_status) {
      if (link_state == LINK_STATE_TRANSMIT_WAIT) {
        if (irq2_status & SX12xx_IRQ_TX_DONE) {
          irq2_status = 0;
          link_state = LINK_STATE_RECEIVE;
        }
      } else
      if (link_state == LINK_STATE_RECEIVE_WAIT) {
        if (irq2_status & SX12xx_IRQ_RX_DONE) {
          irq2_status = 0;
          bool do_clock_reset = (link_rx1_status == RX_STATUS_NONE);
          link_rx2_status = do_receive(ANTENNA_2, do_clock_reset);
        }
      }

      if (irq2_status & SX12xx_IRQ_RX_DONE) { // R, T, TW
        LED_GREEN_ON; //LED_GREEN_OFF;
        while (1) { LED_RED_ON; delay_ms(25); LED_RED_OFF; delay_ms(25); }
      }
      if (irq2_status & SX12xx_IRQ_TX_DONE) {
        LED_RED_ON; //LED_RED_OFF;
        while (1) { LED_GREEN_ON; delay_ms(25); LED_GREEN_OFF; delay_ms(25); }
      }
      if (irq2_status & SX12xx_IRQ_TIMEOUT) {
        while (1) { LED_RED_ON; LED_GREEN_OFF; delay_ms(50); LED_RED_OFF; LED_GREEN_ON; delay_ms(50); }
      }
    }//end of if(irq2_status)
)

    // this happens ca 1 ms after a frame was or should have been received
    if (doPostReceive) {
      doPostReceive = false;

      bool frame_received, valid_frame_received, invalid_frame_received;
      frame_received = valid_frame_received = invalid_frame_received = false; // to make compiler happy
      if (USE_ANTENNA1 && USE_ANTENNA2) {
        frame_received = (link_rx1_status > RX_STATUS_NONE) || (link_rx2_status > RX_STATUS_NONE);
        valid_frame_received = (link_rx1_status > RX_STATUS_INVALID) || (link_rx2_status > RX_STATUS_INVALID);
        invalid_frame_received = frame_received && !valid_frame_received;
      } else if (USE_ANTENNA1) {
        frame_received = (link_rx1_status > RX_STATUS_NONE);
        valid_frame_received = (link_rx1_status > RX_STATUS_INVALID);
        invalid_frame_received = (link_rx1_status == RX_STATUS_INVALID); //frame_received && !valid_frame_received;
      } else if (USE_ANTENNA2) {
        frame_received = (link_rx2_status > RX_STATUS_NONE);
        valid_frame_received = (link_rx2_status > RX_STATUS_INVALID);
        invalid_frame_received = (link_rx2_status == RX_STATUS_INVALID);
      }

dbg.puts("\n> 1: ");
dbg.puts(s8toBCD_s(stats.last_rx_rssi1));
dbg.puts(" 2: ");
dbg.puts(s8toBCD_s(stats.last_rx_rssi2));

      if (frame_received) { // frame received
        uint8_t antenna = ANTENNA_1;

        if (USE_ANTENNA1 && USE_ANTENNA2) {
          // work out which antenna we choose
          //            |   NONE   |  INVALID  | CRC1_VALID | VALID
          // --------------------------------------------------------
          // NONE       |          |   1 or 2  |     1      |  1
          // INVALID    |  1 or 2  |   1 or 2  |     1      |  1
          // CRC1_VALID |    2     |     2     |   1 or 2   |  1
          // VALID      |    2     |     2     |     2      |  1 or 2
          if (link_rx1_status == link_rx2_status) {
            // we can choose either antenna, so select the one with the better rssi
            antenna = (stats.last_rx_rssi1 > stats.last_rx_rssi2) ? ANTENNA_1 : ANTENNA_2;
          } else
          if (link_rx1_status == RX_STATUS_VALID) {
            antenna = ANTENNA_1;
          } else
          if (link_rx2_status == RX_STATUS_VALID) {
            antenna = ANTENNA_2;
          } else
          if (link_rx1_status == RX_STATUS_CRC1_VALID) {
            antenna = ANTENNA_1;
          } else
          if (link_rx2_status == RX_STATUS_CRC1_VALID) {
            antenna = ANTENNA_2;
          } else {
            // we can choose either antenna, so select the one with the better rssi
            antenna = (stats.last_rx_rssi1 > stats.last_rx_rssi2) ? ANTENNA_1 : ANTENNA_2;
          }
        } else if (USE_ANTENNA2) {
          antenna = ANTENNA_2;
        }

        handle_receive(antenna);

dbg.puts(" a "); dbg.puts((antenna == ANTENNA_1) ? "1 " : "2 ");
      }

      if (valid_frame_received) { // valid frame received
        switch (connect_state) {
        case CONNECT_STATE_LISTEN:
          connect_state = CONNECT_STATE_SYNC;
          connect_sync_cnt = 0;
          break;
        case CONNECT_STATE_SYNC:
          connect_sync_cnt++;
          if (connect_sync_cnt >= CONNECT_SYNC_CNT) connect_state = CONNECT_STATE_CONNECTED;
          break;
        default:
          connect_state = CONNECT_STATE_CONNECTED;
        }
        connect_tmo_cnt = CONNECT_TMO_SYSTICKS;
        connect_occured_once = true;

        link_state = LINK_STATE_TRANSMIT; // switch to TX
      }

      // when in listen: we received something, but something wrong, so we need go back to RX
      if ((connect_state == CONNECT_STATE_LISTEN) && invalid_frame_received) {
        link_state = LINK_STATE_RECEIVE;
      }

      // when in listen, slowly loop through frequencies
      if (connect_state == CONNECT_STATE_LISTEN) {
        connect_listen_cnt++;
        if (connect_listen_cnt >= CONNECT_LISTEN_HOP_CNT) {
          fhss.HopToNext();
          connect_listen_cnt = 0;
          link_state = LINK_STATE_RECEIVE; // switch back to RX
        }
      }

      // we just disconnected, or are in sync but don't receive anything
      if ((connect_state >= CONNECT_STATE_SYNC) && !connect_tmo_cnt) {
        // switch to listen state
        // only do it if not in listen, since otherwise it never could reach receive wait and hence never could connect
        connect_state = CONNECT_STATE_LISTEN;
        connect_listen_cnt = 0;
        link_state = LINK_STATE_RECEIVE; // switch back to RX
      }

      // we didn't receive a valid frame
      if ((connect_state >= CONNECT_STATE_SYNC) && !valid_frame_received) {
        frame_missed = true;
        // reset sync counter, relevant if in sync
        connect_sync_cnt = 0;
        // switch to transmit state
        // only do it if receiving, else keep it in RX mode, otherwise chances to connect are dim
        // we are on the correct frequency, so no need to hop
        link_state = LINK_STATE_TRANSMIT;
      }

#ifdef DEVICE_HAS_SX127x
      if ((connect_state >= CONNECT_STATE_SYNC) || (link_state == LINK_STATE_RECEIVE)) {
#else
        if (connect_state >= CONNECT_STATE_SYNC) {
#endif
        sx.SetToIdle();
        sx2.SetToIdle();
      }

      if (!connected()) stats.Clear();
      rxstats.Next();

      doPostReceive2_cnt = 5; // allow link_state changes to be handled, so postpone this few loops
    }//end of if(doPostReceive)

    if (doPostReceive2_cnt) {
      doPostReceive2_cnt--;
      if (!doPostReceive2_cnt) doPostReceive2 = true;
    }
    if (doPostReceive2) {
      doPostReceive2 = false;

      out.SetChannelOrder(Setup.Rx.ChannelOrder);
      if (connected()) {
        out.SendRcData(&rcData, frame_missed, false);
        out.SendLinkStatistics();
      } else {
        switch (Setup.Rx.FailsafeMode) {
        case FAILSAFE_MODE_CH1CH4_CENTER_SIGNAL:
          if (connect_occured_once) {
            tRcData rc;
            memcpy(&rc, &rcData, sizeof(tRcData));
            for (uint8_t n = 0; n < 3; n++) rc.ch[n] = 1024;
            out.SendRcData(&rc, true, true);
          }
          break;
        //default:
          // no signal, so do nothing
        };
        if (connect_occured_once) {
          out.SendLinkStatisticsDisconnected();
        }
      }
    }//end of if(doPostReceive2)

    out.Do(micros());

  }//end of while(1) loop

}//end of main

