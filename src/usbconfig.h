/* usbconfig.h - V-USB configuration for DinoPlayer (HID Keyboard)
 *
 * Based on V-USB example configuration.
 * ATtiny85 DigiSpark: USB D- on PB3, USB D+ on PB4 (INT0 via PCINT)
 */

#ifndef __usbconfig_h_included__
#define __usbconfig_h_included__

/* ---------------------------- Hardware Config ---------------------------- */

#define USB_CFG_IOPORTNAME      B
#define USB_CFG_DMINUS_BIT      3
#define USB_CFG_DPLUS_BIT       4

#define USB_CFG_CLOCK_KHZ       (F_CPU/1000)
#define USB_CFG_CHECK_CRC       0

/* ----------------------- Optional Hardware Config ------------------------ */

/* No external pullup - using DigiSpark's onboard pullup */

/* --------------------------- Functional Range ---------------------------- */

#define USB_CFG_HAVE_INTRIN_ENDPOINT    1
#define USB_CFG_HAVE_INTRIN_ENDPOINT3   0
#define USB_CFG_EP3_NUMBER              3
#define USB_CFG_IMPLEMENT_HALT          0
#define USB_CFG_SUPPRESS_INTR_CODE      0
#define USB_CFG_INTR_POLL_INTERVAL      10
#define USB_CFG_IS_SELF_POWERED         0
#define USB_CFG_MAX_BUS_POWER           100
#define USB_CFG_IMPLEMENT_FN_WRITE      0
#define USB_CFG_IMPLEMENT_FN_READ       0
#define USB_CFG_IMPLEMENT_FN_WRITEOUT   0
#define USB_CFG_HAVE_FLOWCONTROL        0
#define USB_CFG_LONG_TRANSFERS          0
#define USB_COUNT_SOF                   0
#define USB_CFG_CHECK_DATA_TOGGLING     0
#define USB_CFG_HAVE_MEASURE_FRAME_LENGTH   1
#include "osccal.h"
#define USB_USE_FAST_CRC                0

/* -------------------------- Device Description --------------------------- */

#define USB_CFG_VENDOR_ID       0xc0, 0x16  /* obdev shared VID */
#define USB_CFG_DEVICE_ID       0xdc, 0x27  /* unique PID for this project */
#define USB_CFG_DEVICE_VERSION  0x00, 0x01

#define USB_CFG_VENDOR_NAME     'd','i','g','i','s','t','u','m','p','.','c','o','m'
#define USB_CFG_VENDOR_NAME_LEN 13

#define USB_CFG_DEVICE_NAME     'D','i','n','o','P','l','a','y','e','r'
#define USB_CFG_DEVICE_NAME_LEN 10

#define USB_CFG_DEVICE_CLASS        0
#define USB_CFG_DEVICE_SUBCLASS     0
#define USB_CFG_INTERFACE_CLASS     0x03  /* HID */
#define USB_CFG_INTERFACE_SUBCLASS  0x01  /* Boot interface subclass */
#define USB_CFG_INTERFACE_PROTOCOL  0x01  /* Keyboard protocol */

/* Standard boot keyboard report descriptor = 63 bytes */
#define USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH    63

/* ------------------- Descriptor Properties ------------------- */

#define USB_CFG_DESCR_PROPS_DEVICE                  0
#define USB_CFG_DESCR_PROPS_CONFIGURATION           0
#define USB_CFG_DESCR_PROPS_STRINGS                 0
#define USB_CFG_DESCR_PROPS_STRING_0                0
#define USB_CFG_DESCR_PROPS_STRING_VENDOR           0
#define USB_CFG_DESCR_PROPS_STRING_PRODUCT          0
#define USB_CFG_DESCR_PROPS_STRING_SERIAL_NUMBER    0
#define USB_CFG_DESCR_PROPS_HID                     0
#define USB_CFG_DESCR_PROPS_HID_REPORT              0
#define USB_CFG_DESCR_PROPS_UNKNOWN                 0

/* ----------------------- Optional MCU Description ------------------------ */

#ifndef SIG_INTERRUPT0
#define SIG_INTERRUPT0  _VECTOR(1)
#endif

/* ATtiny85: Use pin change interrupt for USB */
#if defined (__AVR_ATtiny45__) || defined (__AVR_ATtiny85__)
#define USB_INTR_CFG            PCMSK
#define USB_INTR_CFG_SET        (1<<USB_CFG_DPLUS_BIT)
#define USB_INTR_ENABLE_BIT     PCIE
#define USB_INTR_PENDING_BIT    PCIF
#define USB_INTR_VECTOR         SIG_PIN_CHANGE
#endif

#endif /* __usbconfig_h_included__ */
