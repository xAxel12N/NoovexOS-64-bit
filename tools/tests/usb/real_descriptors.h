#ifndef REAL_DESCRIPTORS_H
#define REAL_DESCRIPTORS_H

/*
 * HID data for the v11.01 "pointer moves only left/right" bug
 * (Biltema B-1050W, Art. 24-593: 062A:4101 or 248A:16BB).
 *
 * Status of each entry:
 *   MEASURED      bytes captured from a real device and published
 *   RECONSTRUCTED descriptor written by us to match a measured layout;
 *                 the real descriptor bytes were not found anywhere public
 *
 * No public dump of the raw report descriptor bytes was found for
 * 062A:4101, 248A:16BB, 248A:8367, 248A:8366, 25A7:FA61, 1D57:FA60,
 * 3938:1031 or 062A:5918. Every linux-hardware.org lsusb log checked for
 * 062A:4101 prints "Report Descriptors: ** UNAVAILABLE **" because usbhid
 * holds the interface.
 */

/*
 * 062A:4101 MOSART Semi. "2.4G Keyboard Mouse", bcdDevice 2.09
 * Source (lsusb -v): https://raw.githubusercontent.com/h3rl/jetsonfun/HEAD/hids/mokbhid.txt
 * Also matched by linux-hardware.org probes 475a1011ab and ddf6e5a3f7 (log=lsusb).
 *   if0: class 3 sub 1 proto 1 (boot keyboard), report desc 65 bytes, EP 0x81 IN, mps 8, bInterval 10
 *   if1: class 3 sub 1 proto 2 (boot mouse),    report desc 168 bytes, EP 0x82 IN, mps 7, bInterval 4
 */
#define MOSART_4101_IF0_RDLEN 65
#define MOSART_4101_IF1_RDLEN 168
#define MOSART_4101_IF1_MPS   7

/*
 * 062A:4101 mouse layout in REPORT protocol, MEASURED on real hardware.
 * Source: https://raw.githubusercontent.com/family-mruby/fmruby-core/HEAD/doc/mouse_wheel/report/w3.md
 *   (and flash/etc/hid_devices.toml in the same repository)
 * Report length went 3 -> 7 bytes after SET_PROTOCOL(Report).
 *   byte 0       report ID 0x03
 *   payload bits 0..7    buttons
 *   payload bits 8..19   X, 12-bit signed
 *   payload bits 20..31  Y, 12-bit signed (starts in the HIGH nibble of byte 3 of the raw report)
 *   payload bits 32..39  wheel, 8-bit signed
 *   payload bits 40..47  one more byte (zero in every capture; likely AC Pan)
 */
#define MOSART_4101_MOUSE_ID     0x03
#define MOSART_4101_MOUSE_LEN    7
#define MOSART_4101_X_OFF        8
#define MOSART_4101_Y_OFF        20
#define MOSART_4101_XY_SZ        12
#define MOSART_4101_WHEEL_OFF    32

static const unsigned char MOSART_4101_RPT_RIGHT1[7] = {0x03,0x00,0x01,0x00,0x00,0x00,0x00};
static const unsigned char MOSART_4101_RPT_DOWN1[7]  = {0x03,0x00,0x00,0x10,0x00,0x00,0x00};
static const unsigned char MOSART_4101_RPT_WHEELM1[7]= {0x03,0x00,0x00,0x00,0x00,0xFF,0x00};

/*
 * 062A:4101 if1, RECONSTRUCTED (not the real 168 bytes): the mouse
 * collection only, written to produce exactly the measured layout above.
 * Buttons 1..5 + 3 pad bits, X/Y Report Size 12 Count 2, wheel, AC Pan.
 */
static const unsigned char RD_MOSART_4101_MOUSE_RECON[] = {
 0x05,0x01, 0x09,0x02, 0xA1,0x01, 0x85,0x03, 0x09,0x01, 0xA1,0x00,
 0x05,0x09, 0x19,0x01, 0x29,0x05, 0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x05, 0x81,0x02,
 0x75,0x03, 0x95,0x01, 0x81,0x01,
 0x05,0x01, 0x09,0x30, 0x09,0x31, 0x16,0x01,0xF8, 0x26,0xFF,0x07, 0x75,0x0C, 0x95,0x02, 0x81,0x06,
 0x09,0x38, 0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x01, 0x81,0x06,
 0x05,0x0C, 0x0A,0x38,0x02, 0x95,0x01, 0x81,0x06,
 0xC0, 0xC0
};

/*
 * Sample report for the reconstructed layout, dx=+5 dy=-3, no buttons:
 *   X = 0x005 at payload bits 8..19, Y = 0xFFD at payload bits 20..31
 *   payload byte1 = 0x05, byte2 = 0xD0 (X high nibble 0, Y low nibble D), byte3 = 0xFF
 */
static const unsigned char MOSART_4101_RPT_DX5_DYM3[7] = {0x03,0x00,0x05,0xD0,0xFF,0x00,0x00};
#define MOSART_4101_RPT_DX5_DYM3_DX 5
#define MOSART_4101_RPT_DX5_DYM3_DY (-3)

/* dx=-200 dy=+150: X=0xF38, Y=0x096 -> bytes 38 6F 09 */
static const unsigned char MOSART_4101_RPT_DXM200_DY150[7] = {0x03,0x00,0x38,0x6F,0x09,0x00,0x00};
#define MOSART_4101_RPT_DXM200_DY150_DX (-200)
#define MOSART_4101_RPT_DXM200_DY150_DY 150

/*
 * Same motion truncated to 4 bytes (ID + 3 payload bytes). With our
 * hid_bits() X (bits 8..19) still fits in 24 bits but Y (bits 20..31)
 * does not, so X decodes and Y silently reads 0. This reproduces the
 * reported symptom exactly.
 */
static const unsigned char MOSART_4101_RPT_DX5_DYM3_TRUNC4[4] = {0x03,0x00,0x05,0xD0};

/* 062A:4101 in BOOT protocol (MEASURED by the same source: fixed 3 bytes, no ID). */
static const unsigned char MOSART_4101_BOOT_DX5_DYM3[3] = {0x00,0x05,0xFD};

/*
 * 248A:16BB "Wireless Receiver", 248A:8367 / 248A:8366 (Maxxter/Telink
 * receivers), 25A7:FA61, 1D57:FA60, 3938:1031, 062A:5918:
 * no raw descriptor bytes and no measured report layout found.
 */

#endif
