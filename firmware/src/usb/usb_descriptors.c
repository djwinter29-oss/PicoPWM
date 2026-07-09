/**
 * @file usb_descriptors.c
 * @brief TinyUSB descriptor tables and callbacks for PicoPWM USB CDC.
 */

#include "bsp/board_api.h"
#include "tusb.h"
#include <string.h>

/** @brief USB vendor ID used by the PicoPWM firmware image. */
#define USB_VID 0xCafe
/** @brief USB product ID used by the PicoPWM firmware image. */
#define USB_PID 0x4010
/** @brief USB device version advertised in the device descriptor. */
#define USB_BCD  0x0210

/** @brief Manufacturer string exposed in the USB string table. */
#define USB_STR_MANUFACTURER "PicoPWM"
/** @brief Product string exposed in the USB string table. */
#define USB_STR_PRODUCT "PicoPWM USB CDC"
/** @brief Interface string for the CDC command interface. */
#define USB_STR_CDC "CDC Command"

/** @brief USB interface numbering used inside the configuration descriptor. */
typedef enum {
    ITF_NUM_CDC = 0, /**< CDC control interface number. */
    ITF_NUM_CDC_DATA, /**< CDC data interface number. */
    ITF_NUM_TOTAL /**< Total number of interfaces in the configuration. */
} usb_interface_number_t;

/** @brief USB string descriptor indices used by the device and configuration descriptors. */
typedef enum {
    STRID_LANGID = 0, /**< Language ID string descriptor index. */
    STRID_MANUFACTURER, /**< Manufacturer string descriptor index. */
    STRID_PRODUCT, /**< Product string descriptor index. */
    STRID_SERIAL, /**< Serial number string descriptor index. */
    STRID_CDC, /**< CDC interface string descriptor index. */
} usb_string_id_t;

/** @brief USB device descriptor returned to the host during enumeration. */
static tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0101,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

/**
 * @brief TinyUSB callback returning the device descriptor.
 * @return Pointer to the static device descriptor.
 */
uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/** @brief Endpoint number for CDC notification IN traffic. */
#define EPNUM_CDC_NOTIF 0x81
/** @brief Endpoint number for CDC data OUT traffic. */
#define EPNUM_CDC_OUT   0x02
/** @brief Endpoint number for CDC data IN traffic. */
#define EPNUM_CDC_IN    0x82
/** @brief Total bytes in the one and only USB configuration descriptor. */
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

/** @brief Configuration descriptor containing only the CDC command interface. */
static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

/**
 * @brief TinyUSB callback returning the configuration descriptor.
 * @param index Configuration index requested by the host.
 * @return Pointer to the static configuration descriptor.
 */
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/** @brief USB string table backing the TinyUSB string-descriptor callback. */
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    USB_STR_MANUFACTURER,
    USB_STR_PRODUCT,
    NULL,
    USB_STR_CDC,
}

/** @brief Scratch UTF-16 string descriptor buffer returned by TinyUSB string callbacks. */
static uint16_t desc_str[32 + 1];

/**
 * @brief TinyUSB callback returning one UTF-16 USB string descriptor.
 * @param index String descriptor index requested by the host.
 * @param langid Language ID requested by the host.
 * @return Pointer to the UTF-16 descriptor buffer, or `NULL` for an invalid index.
 */
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    switch (index) {
    case STRID_LANGID:
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
        break;
    case STRID_SERIAL:
        chr_count = board_usb_get_serial(desc_str + 1, 32);
        break;
    default:
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }
        {
            const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        for (size_t i = 0; i < chr_count; i++) {
            desc_str[1 + i] = (uint16_t)str[i];
        }
        }
        break;
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}