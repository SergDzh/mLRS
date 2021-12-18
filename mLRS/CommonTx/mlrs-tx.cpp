//*******************************************************
// MLRS project
// Copyright (c) OlliW, OlliW42, www.olliw.eu
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
//*******************************************************
//
/********************************************************

v0.0.00:
*/

#define DBG_MAIN(x)
#define DBG_MAIN_SLIM(x)
#define DBG_MAIN_FULL(x)


// we set the priorities here to have an overview
#define SX_DIO1_EXTI_IRQ_PRIORITY   11
#define UART_IRQ_PRIORITY           10 // mbridge, this needs to be high, when lower than DIO1, the module could stop sending via the bridge
#define UARTB_IRQ_PRIORITY          14 // serial
#define UARTC_IRQ_PRIORITY          14

#include "..\Common\common_conf.h"
#include "..\Common\hal\glue.h"
#include "..\modules\stm32ll-lib\src\stdstm32.h"
#include "..\modules\stm32ll-lib\src\stdstm32-peripherals.h"
#include "..\Common\hal\tx-hal-siyi-f103c8.h"
#include "..\modules\stm32ll-lib\src\stdstm32-delay.h"
#include "..\modules\stm32ll-lib\src\stdstm32-spi.h"
#include "..\modules\sx12xx-lib\src\sx128x.h"
#include "..\modules\stm32ll-lib\src\stdstm32-uartb.h"
#include "..\modules\stm32ll-lib\src\stdstm32-uartc.h"
#include "..\Common\fhss.h"
#define FASTMAVLINK_IGNORE_WADDRESSOFPACKEDMEMBER
#include "..\Common\mavlink\out\mlrs\mlrs.h"
#include "..\Common\common.h"

#include "mbridge_interface.h"
#include "txstats.h"


#define CLOCK_TIMx                  TIM3

void clock_init(void)
{
  tim_init_1us_freerunning(CLOCK_TIMx);
}

uint16_t micros(void)
{
  return CLOCK_TIMx->CNT;
}


void init(void)
{
  leds_init();
  button_init();
  pos_switch_init();

  delay_init();
  clock_init();
  serial.Init();

  uartc_init();

  sx.Init();
}


//-------------------------------------------------------
// Statistics for Transmitter
//-------------------------------------------------------

TxStats txstats;


//-------------------------------------------------------
// mavlink
//-------------------------------------------------------

#include "mavlink_interface.h"


//-------------------------------------------------------
// SX1280
//-------------------------------------------------------

volatile uint16_t irq_status;

IRQHANDLER(
void SX_DIO1_EXTI_IRQHandler(void)
{
  LL_EXTI_ClearFlag_0_31(SX_DIO1_EXTI_LINE_x);
  //LED_RIGHT_RED_TOGGLE;
  irq_status = sx.GetIrqStatus();
  sx.ClearIrqStatus(SX1280_IRQ_ALL);
  if (irq_status & SX1280_IRQ_RX_DONE) {
    sx.ReadFrame((uint8_t*)&rxFrame, FRAME_TX_RX_LEN);
  }
})


typedef enum {
    LINK_STATE_IDLE = 0,

    LINK_STATE_TRANSMIT,
    LINK_STATE_TRANSMIT_WAIT,
    LINK_STATE_RECEIVE,
    LINK_STATE_RECEIVE_WAIT,
} LINK_STATE_ENUM;


void do_transmit(void) // we send a TX frame to receiver
{
  uint8_t payload[FRAME_TX_PAYLOAD_LEN] = {0};
  uint8_t payload_len = 0;

  for (uint8_t i = 0; i < FRAME_TX_PAYLOAD_LEN; i++) {
#if (SETUP_TX_USE_MBRIDGE == 1)
    if (!bridge.available()) break;
    payload[payload_len] = bridge.getc();
#else
    if (!serial.available()) break;
    payload[payload_len] = serial.getc();
#endif
    payload_len++;
  }

  sx.GetPacketStatus(&stats.last_rssi, &stats.last_snr);
  tFrameStats frame_stats;
  frame_stats.seq_no = 0; // not used
  frame_stats.rssi = stats.last_rssi;
  frame_stats.snr = stats.last_snr;
  frame_stats.LQ = txstats.GetLQ();

  pack_tx_frame(&txFrame, &frame_stats, &rcData, payload, payload_len);
  sx.SendFrame((uint8_t*)&txFrame, FRAME_TX_RX_LEN, 10); // 10 ms tmo
}


bool do_receive(void) // we receive a RX frame from receiver
{
  uint16_t err = check_rx_frame(&rxFrame);

  if (err) {
    DBG_MAIN(uartc_puts("fail "); uartc_putc('\n');)
uartc_puts("fail "); uartc_puts(u16toHEX_s(err));uartc_putc('\n');
    return false;
  }

  return true;
}


void process_received_frame(void)
{
  stats.received_seq_no = rxFrame.status.seq_no;
  stats.received_rssi = -(rxFrame.status.rssi_u8);
  stats.received_LQ = rxFrame.status.LQ;

  for (uint8_t i = 0; i < rxFrame.status.payload_len; i++) {
    uint8_t c = rxFrame.payload[i];
#if (SETUP_TX_USE_MBRIDGE == 1)
    bridge.putc(c); // send to radio
#else
    serial.putc(c); // send to serial
#endif

    uint8_t res = fmav_parse_to_frame_buf(&f_result, f_buf, &f_status, c);
    if (res == FASTMAVLINK_PARSE_RESULT_OK) { // we have a complete mavlink frame
//      uartc_puts("m");
      if (inject_radio_status) {
        inject_radio_status = false;
#if (SETUP_TX_SEND_RADIO_STATUS == 1)
        send_radio_status();
#elif (SETUP_TX_SEND_RADIO_STATUS == 2)
        send_radio_status_v2();
#endif
      }
    }
  }

  DBG_MAIN(char s[16];
  u8toBCDstr(rxFrame.status.seq_no, s);
  uartc_puts("got "); uartc_puts(s); uartc_puts(": ");
  for (uint8_t i = 0; i < rxFrame.status.payload_len; i++) uartc_putc(rxFrame.payload[i]);
  uartc_putc('\n');)
}



//##############################################################################################################
//*******************************************************
// MAIN routine
//*******************************************************

uint16_t led_blink;
uint16_t tick_1hz;

uint16_t tx_tick;
uint16_t link_state;
uint16_t connected_tmo_cnt;


int main_main(void)
{
  init();
#if (SETUP_TX_USE_MBRIDGE == 1)
  bridge.Init();
#endif

  DBG_MAIN(uartc_puts("\n\n\nHello\n\n");)
  uartc_puts("\n\n\nHello\n\n");

  // startup sign of life
  LED_RED_OFF;
  for (uint8_t i = 0; i < 7; i++) { LED_RED_TOGGLE; delay_ms(50); }

  // start up sx
  if (!sx.isOk()) {
    DBG_MAIN(uartc_puts("fail\n");)
    while (1) { LED_RED_TOGGLE; delay_ms(50); } // fail!
  } else {
    DBG_MAIN(uartc_puts("ok\n");)
  }
  sx.StartUp();
  fhss.Init(SEEDDBLWORD);
  fhss.StartTx();
  sx.SetRfFrequency(fhss.GetCurr());

//  for (uint8_t n = 0; n < FHSS_MAX_NUM; n++) {
//    uartc_puts("f = "); uartc_puts(u32toBCD_s(fhss.fhss_list[n])); uartc_puts("\n");
//  }

  link_state = LINK_STATE_IDLE;
  tx_tick = 0;
  connected_tmo_cnt = 0;
  txstats.Init(LQ_AVERAGING_PERIOD);
  f_init();

  led_blink = 0;
  tick_1hz = 0;
  doSysTask = 0; // helps in avoiding too short first loop
  while (1) {

    //-- SysTask handling

    if (doSysTask) {
      doSysTask = 0;

      if (connected_tmo_cnt) {
        connected_tmo_cnt--;
      }

      DECc(tick_1hz, SYSTICK_DELAY_MS(1000));
      DECc(tx_tick, SYSTICK_DELAY_MS(FRAME_RATE_MS));
      if (connected_tmo_cnt) {
        DECc(led_blink, SYSTICK_DELAY_MS(500));
      } else {
        DECc(led_blink, SYSTICK_DELAY_MS(200));
      }

      if (!led_blink) {
        if (connected_tmo_cnt) LED_GREEN_TOGGLE; else LED_RED_TOGGLE;
      }
      if (connected_tmo_cnt) { LED_RED_OFF; } else { LED_GREEN_OFF; }

      if (!connected_tmo_cnt) {
        f_init();
      }

      if (!tick_1hz) {
        txstats.Update1Hz();
        if (connected_tmo_cnt) inject_radio_status = true;
        //uartc_puts(".");

        uartc_puts("TX: ");
        uartc_puts(u8toBCD_s(txstats.GetRawLQ())); uartc_putc(',');
        uartc_puts(u8toBCD_s(stats.LQ));
        uartc_puts(" (");
        uartc_puts(u8toBCD_s(stats.LQ_received)); uartc_putc(',');
        uartc_puts(u8toBCD_s(stats.LQ_transmitted)); uartc_putc(',');
        uartc_puts(u8toBCD_s(stats.LQ_valid_received));
        uartc_puts("),");
        uartc_puts(u8toBCD_s(stats.received_LQ)); uartc_puts(", ");

        uartc_puts(s8toBCD_s(stats.last_rssi)); uartc_putc(',');
        uartc_puts(s8toBCD_s(stats.received_rssi)); uartc_puts(", ");
        uartc_puts(s8toBCD_s(stats.last_snr));
        uartc_putc('\n');
      }

      if (!tx_tick) {
        // trigger next cycle
        txstats.Next();
        link_state = LINK_STATE_TRANSMIT;
      }
    }

    //-- SX handling

    switch (link_state) {
    case LINK_STATE_IDLE:
      break;

    case LINK_STATE_TRANSMIT:
      fhss.HopToNext();
      sx.SetRfFrequency(fhss.GetCurr());
      do_transmit();
      link_state = LINK_STATE_TRANSMIT_WAIT;
      DBG_MAIN_FULL(uartc_puts("TX: tx\n");)
      DBG_MAIN_SLIM(uartc_puts(">");)
      break;

    case LINK_STATE_RECEIVE:
      // datasheet says "As soon as a packet is detected, the timer is automatically
      // disabled to allow complete reception of the packet." Why does then 5 ms not work??
      sx.SetToRx(10); // we wait 10 ms for the start for the frame, 5 ms does not work ??
      link_state = LINK_STATE_RECEIVE_WAIT;
      DBG_MAIN_FULL(uartc_puts("TX: rx\n");)
      break;
    }

    if (irq_status) {
      if (link_state == LINK_STATE_TRANSMIT_WAIT) {
        if (irq_status & SX1280_IRQ_TX_DONE) {
          irq_status = 0;
          txstats.SetFrameTransmitted();
          link_state = LINK_STATE_RECEIVE;
          DBG_MAIN_FULL(uartc_puts("TX: tx done\n");)
          DBG_MAIN_SLIM(uartc_puts("!");)
        }
      }
      else
      if (link_state == LINK_STATE_RECEIVE_WAIT) {
        if (irq_status & SX1280_IRQ_RX_DONE) {
          irq_status = 0;
          if (do_receive()) {
            process_received_frame();
            txstats.SetValidFrameReceived();
            connected_tmo_cnt = CONNECT_TMO_SYSTICKS;
          }
          txstats.SetFrameReceived();
          link_state = LINK_STATE_IDLE; // ready for next frame
          DBG_MAIN_FULL(uartc_puts("TX: rx done\n");)
          DBG_MAIN_SLIM(uartc_puts("<\n");)
        }
      }

      if (irq_status & SX1280_IRQ_RX_TX_TIMEOUT) {
        irq_status = 0;
        link_state = LINK_STATE_IDLE;
        DBG_MAIN_FULL(uartc_puts("TX: tmo\n");)
      }

      if (irq_status & SX1280_IRQ_RX_DONE) {
        LED_GREEN_OFF;
        while (1) { LED_RED_ON; delay_ms(5); LED_RED_OFF; delay_ms(5); }
      }
      if (irq_status & SX1280_IRQ_TX_DONE) {
        LED_RED_OFF;
        while (1) { LED_GREEN_ON; delay_ms(5); LED_GREEN_OFF; delay_ms(5); }
      }
    }

    //-- Bridge handling
#if (SETUP_TX_USE_MBRIDGE == 1)
    if (bridge.channels_updated) {
      bridge.channels_updated = 0;
      // send stats to transmitter
      uint8_t cmd = 0x01;
      uint8_t payload[MBRIDGE_COMMANDPACKET_TX_SIZE] = {0};
      payload[0] = -(stats.last_rssi);
      payload[1] = txstats.GetLQ();
      payload[2] = -(stats.received_rssi);
      payload[3] = stats.received_LQ;
      payload[4] = txstats.GetFramesReceivedLQ();
      payload[5] = stats.LQ_received;
      payload[6] = stats.LQ_valid_received;
      bridge.cmd_to_transmitter(cmd, payload, MBRIDGE_COMMANDPACKET_TX_SIZE);
#  if (SETUP_TX_CHANNELS_SOURCE == 1)
      // update channels
      fill_rcdata_from_mbridge(&rcData, &(bridge.channels));
#  endif
    }

    if (bridge.cmd_received) {
      uint8_t cmd;
      uint8_t payload[MBRIDGE_COMMANDPACKET_RX_SIZE];
      bridge.cmd_from_transmitter(&cmd, payload);
    }
#endif

  }//end of while(1) loop

}//end of main

