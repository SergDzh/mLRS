//*******************************************************
// Copyright (c) MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
// OlliW @ www.olliw.eu
//*******************************************************
// Mavlink Interface RX Side
//*******************************************************
#ifndef MAVLINK_INTERFACE_RX_H
#define MAVLINK_INTERFACE_RX_H
#pragma once


#include "../Common/mavlink/fmav_extension.h"

static inline bool connected(void);

#define RADIO_LINK_SYSTEM_ID        51 // SiK uses 51, 68
#define GCS_SYSTEM_ID               255 // default of MissionPlanner, QGC

#define MAVLINK_BUF_SIZE            300 // needs to be larger than max mavlink frame size = 286 bytes


class MavlinkBase
{
  public:
    void Init(void);
    void Do(void);
    void SendRcData(tRcData* rc_out, bool failsafe);

    void lost_data(void);
    void putc(char c);
    bool available(void);
    uint8_t getc(void);
    void flush(void);

  private:
    void generate_radio_status(void);
    void generate_rc_channels_override(void);
    void generate_rc_channels(void);
    void send_msg_serial_out(void);

    fmav_status_t status_link_in;
    fmav_result_t result_link_in;
    uint8_t buf_link_in[MAVLINK_BUF_SIZE]; // buffer for link in parser
    fmav_status_t status_serial_out;
    fmav_message_t msg_serial_out;

    uint8_t _buf[MAVLINK_BUF_SIZE]; // working buffer

    // to inject RADIO_STATUS messages
    bool inject_radio_status;
    uint32_t radio_status_tlast_ms;

    uint32_t bytes_serial_in;

    // to inject RC_CHANNELS_OVERRDIE, RC_CHANNELS
    bool inject_rc_channels;
    uint16_t rc_chan[16]; // holds the rc data in MAVLink format
};


void MavlinkBase::Init(void)
{
    fmav_init();

    result_link_in = {0};
    status_link_in = {0};
    status_serial_out = {0};

    inject_radio_status = false;
    radio_status_tlast_ms = millis32() + 1000;

    bytes_serial_in = 0;

    inject_rc_channels = false;
    for (uint8_t i = 0; i < 16; i++) rc_chan[i] = 0;
}


void MavlinkBase::SendRcData(tRcData* rc_out, bool failsafe)
{
    if (Setup.Rx.SendRcChannels == SEND_RC_CHANNELS_OFF) return;

    uint8_t failsafe_mode = Setup.Rx.FailsafeMode;

    if (failsafe) {
        switch (failsafe_mode) {
        case FAILSAFE_MODE_NO_SIGNAL:
            // we do not output anything, so jump out
            return;
        }
    }
/*
    rc_chan[0] = (((int32_t)(rc_out->ch[0]) - 1024) * 1200) / 2047 + 1500; // 1200 = 1920 * 5/8
    rc_chan[1] = (((int32_t)(rc_out->ch[1]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[2] = (((int32_t)(rc_out->ch[2]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[3] = (((int32_t)(rc_out->ch[3]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[4] = (((int32_t)(rc_out->ch[4]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[5] = (((int32_t)(rc_out->ch[5]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[6] = (((int32_t)(rc_out->ch[6]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[7] = (((int32_t)(rc_out->ch[7]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[8] = (((int32_t)(rc_out->ch[8]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[9] = (((int32_t)(rc_out->ch[9]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[10] = (((int32_t)(rc_out->ch[10]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[11] = (((int32_t)(rc_out->ch[11]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[12] = (((int32_t)(rc_out->ch[12]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[13] = (((int32_t)(rc_out->ch[13]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[14] = (((int32_t)(rc_out->ch[14]) - 1024) * 1200) / 2047 + 1500;
    rc_chan[15] = (((int32_t)(rc_out->ch[15]) - 1024) * 1200) / 2047 + 1500;
*/
    for (uint8_t i = 0; i < 16; i++) rc_chan[i] = rc_to_mavlink(rc_out->ch[i]);

    inject_rc_channels = true;
}


void MavlinkBase::Do(void)
{
    uint32_t tnow_ms = millis32();

    if (!connected()) {
        //Init();
        inject_radio_status = false;
        radio_status_tlast_ms = millis32() + 1000;
    }

    if (Setup.Rx.SerialLinkMode != SERIAL_LINK_MODE_MAVLINK) return;

    if ((tnow_ms - radio_status_tlast_ms) >= 1000) {
        radio_status_tlast_ms = tnow_ms;
        if (connected() && Setup.Rx.SendRadioStatus) inject_radio_status = true;
    }

    if (inject_rc_channels) { // && serial.tx_is_empty()) { // give it priority
        inject_rc_channels = false;
        switch (Setup.Rx.SendRcChannels) {
        case SEND_RC_CHANNELS_OVERRIDE:
            generate_rc_channels_override();
            send_msg_serial_out();
            break;
        case SEND_RC_CHANNELS_RCCHANNELS:
            generate_rc_channels();
            send_msg_serial_out();
            break;
        }
    }

    if (inject_radio_status) { // && serial.tx_is_empty()) {
        inject_radio_status = false;
        generate_radio_status();
        send_msg_serial_out();
    }
}


// missing or corrupt frame => reset mavlink parser
void MavlinkBase::lost_data(void)
{
  fmav_status_reset_rx(&status_link_in);
}

void MavlinkBase::putc(char c)
{
    if (fmav_parse_and_check_to_frame_buf(&result_link_in, buf_link_in, &status_link_in, c)) {
        fmav_frame_buf_to_msg(&msg_serial_out, &result_link_in, buf_link_in);

        send_msg_serial_out();
    }
}


bool MavlinkBase::available(void)
{
    return serial.available();
}


uint8_t MavlinkBase::getc(void)
{
    bytes_serial_in++;

    return serial.getc();
}


void MavlinkBase::flush(void)
{
    serial.flush();
}


void MavlinkBase::send_msg_serial_out(void)
{
    uint16_t len = fmav_msg_to_frame_buf(_buf, &msg_serial_out);

    serial.putbuf(_buf, len);
}


// see design_decissions.h for details
void MavlinkBase::generate_radio_status(void)
{
uint8_t rssi, remrssi, txbuf, noise;

    rssi = rssi_i8_to_ap(stats.GetLastRxRssi());
    remrssi = rssi_i8_to_ap(stats.received_rssi);

    // we don't have a reasonable noise measurement, but can use this field to report on the snr
    // the snr can be positive and negative however, so we artificially set snr = 10 to zero
    int16_t snr = -stats.GetLastRxSnr() + 10;
    noise = (snr < 0) ? 0 : (snr > 127) ? 127 : snr;

    txbuf = 100;
    if (Setup.Rx.SendRadioStatus == SEND_RADIO_STATUS_ON_W_TXBUF) {
        // method C
        uint32_t rate_max = ((uint32_t)1000 * FRAME_RX_PAYLOAD_LEN) / Config.frame_rate_ms;
        uint32_t rate_percentage = (bytes_serial_in * 100) / rate_max;
        if (rate_percentage > 80) {
            txbuf = 0; // +60 ms
        } else if (rate_percentage > 70) {
            txbuf = 30; // +20 ms
        } else if (rate_percentage < 45) {
            txbuf = 100; // -40 ms
        } else if (rate_percentage < 55) {
            txbuf = 91; // -20 ms
        } else {
            txbuf = 60; // no change
        }

        if (serial.bytes_available() > 512) txbuf = 0; // keep the buffer low !!

/*dbg.puts("\nM: ");
dbg.puts(u16toBCD_s(stats.GetTransmitBandwidthUsage()*41));dbg.puts(", ");
dbg.puts(u16toBCD_s(bytes_serial_in));dbg.puts(", ");
dbg.puts(u16toBCD_s(serial.bytes_available()));dbg.puts(", ");
dbg.puts(u8toBCD_s(rate_percentage));dbg.puts(", ");
dbg.puts(u8toBCD_s(txbuf));dbg.puts(", ");
if(txbuf<20) dbg.puts("+60 "); else
if(txbuf<40) dbg.puts("+20 "); else
if(txbuf>95) dbg.puts("-40 "); else
if(txbuf>90) dbg.puts("-20 "); else dbg.puts("+-0 ");*/
    }
    bytes_serial_in = 0; // reset, to restart rate measurement

    fmav_msg_radio_status_pack(
        &msg_serial_out,
        RADIO_LINK_SYSTEM_ID, MAV_COMP_ID_TELEMETRY_RADIO, // SiK uses 51, 68
        rssi, remrssi, txbuf, noise, UINT8_MAX, 0, 0,
        //uint8_t rssi, uint8_t remrssi, uint8_t txbuf, uint8_t noise, uint8_t remnoise, uint16_t rxerrors, uint16_t fixed,
        &status_serial_out);
}


void MavlinkBase::generate_rc_channels_override(void)
{
    fmav_msg_rc_channels_override_pack(
        &msg_serial_out,
        GCS_SYSTEM_ID, MAV_COMP_ID_TELEMETRY_RADIO, // ArduPilot accepts it only if it comes from its GCS sysid
        0, 0, // we do not know the sysid, compid of the flight controller
        rc_chan[0], rc_chan[1], rc_chan[2], rc_chan[3], rc_chan[4], rc_chan[5], rc_chan[6], rc_chan[7],
        rc_chan[8], rc_chan[9], rc_chan[10], rc_chan[11], rc_chan[12], rc_chan[13], rc_chan[14], rc_chan[15],
        0,0,
        // uint8_t target_system, uint8_t target_component,
        // uint16_t chan1_raw, uint16_t chan2_raw, uint16_t chan3_raw, uint16_t chan4_raw, uint16_t chan5_raw, uint16_t chan6_raw, uint16_t chan7_raw, uint16_t chan8_raw,
        // uint16_t chan9_raw, uint16_t chan10_raw, uint16_t chan11_raw, uint16_t chan12_raw, uint16_t chan13_raw, uint16_t chan14_raw, uint16_t chan15_raw, uint16_t chan16_raw,
        // uint16_t chan17_raw, uint16_t chan18_raw,
        &status_serial_out);
}


void MavlinkBase::generate_rc_channels(void)
{
    fmav_msg_rc_channels_pack(
        &msg_serial_out,
        RADIO_LINK_SYSTEM_ID, MAV_COMP_ID_TELEMETRY_RADIO,
        millis32(),
        16,
        rc_chan[0], rc_chan[1], rc_chan[2], rc_chan[3], rc_chan[4], rc_chan[5], rc_chan[6], rc_chan[7],
        rc_chan[8], rc_chan[9], rc_chan[10], rc_chan[11], rc_chan[12], rc_chan[13], rc_chan[14], rc_chan[15],
        0,0,
        0,
        // uint32_t time_boot_ms, uint8_t chancount,
        // uint16_t chan1_raw, uint16_t chan2_raw, uint16_t chan3_raw, uint16_t chan4_raw, uint16_t chan5_raw, uint16_t chan6_raw, uint16_t chan7_raw, uint16_t chan8_raw,
        // uint16_t chan9_raw, uint16_t chan10_raw, uint16_t chan11_raw, uint16_t chan12_raw, uint16_t chan13_raw, uint16_t chan14_raw, uint16_t chan15_raw, uint16_t chan16_raw,
        // uint16_t chan17_raw, uint16_t chan18_raw,
        // uint8_t rssi,
        &status_serial_out);
}


#endif // MAVLINK_INTERFACE_RX_H
