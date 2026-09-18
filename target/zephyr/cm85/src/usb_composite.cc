// USB composite device: UAC2 capture (source-only instrument) + MIDI 2.0.
//
// Stage 1 registers both classes and enumerates. The UAC2 ops are stubs —
// Stage 3 replaces them with the SOF-driven data feed into usbd_uac2_send().
//
// Register order is load-bearing: usbd_register_all_classes() would order by
// linker section (usbd_midi2.o before usbd_uac2.o), putting MIDI's AudioControl
// interface at iface 0; snd-usb-audio then takes that as its control interface
// and rejects the UAC2 AS terminal link ("bogus bTerminalLink", no PCM). We
// register explicitly with UAC2 first (usbd_class_append is sys_slist_append,
// so call order == interface order). See brief §4.3.

#include "usb_composite.h"

#include <zephyr/usb/usbd.h>
// usbd_uac2.h lacks extern "C" guards (its declarations get C++ linkage when
// included from C++ and no longer resolve against the C definitions in
// usbd_uac2.c). Wrap it so the unmangled C symbols resolve.
extern "C" {
#include <zephyr/usb/class/usbd_uac2.h>
}
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(usb_composite, LOG_LEVEL_INF);

namespace {

/// Stub UAC2 ops (Stage 1 enumerates; Stage 3 drives the stream).
void Uac2SofStub(const struct device *dev, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
}

void Uac2TerminalStub(const struct device *dev, uint8_t terminal, bool enabled,
		      bool microframes, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(terminal);
	ARG_UNUSED(enabled);
	ARG_UNUSED(microframes);
	ARG_UNUSED(user_data);
}

const struct uac2_ops kUac2Ops = {
	.sof_cb = Uac2SofStub,
	.terminal_update_cb = Uac2TerminalStub,
};

// The composite device context. VID/PID are the Zephyr sample values (the
// prototype has no purchased vendor ID).
USBD_DEVICE_DEFINE(twang_usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   0x2fe3, 0x000f);

USBD_DESC_LANG_DEFINE(twang_lang);
USBD_DESC_MANUFACTURER_DEFINE(twang_mfr, "twang");
USBD_DESC_PRODUCT_DEFINE(twang_product, "twang");

USBD_DESC_CONFIG_DEFINE(twang_fs_cfg, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(twang_hs_cfg, "HS Configuration");

USBD_CONFIGURATION_DEFINE(twang_fs_config, USB_SCD_SELF_POWERED, 250,
			  &twang_fs_cfg);
USBD_CONFIGURATION_DEFINE(twang_hs_config, USB_SCD_SELF_POWERED, 250,
			  &twang_hs_cfg);

/// Register both classes for one speed, UAC2 first (load-bearing order).
int RegisterClasses(struct usbd_context *ctx, enum usbd_speed speed)
{
	int err;

	err = usbd_register_class(ctx, "uac2_0", speed, 1);
	if (err) {
		LOG_ERR("register uac2_0 (%d): %d", speed, err);
		return err;
	}
	err = usbd_register_class(ctx, "midi_0", speed, 1);
	if (err) {
		LOG_ERR("register midi_0 (%d): %d", speed, err);
		return err;
	}
	return 0;
}

}  // namespace

namespace usb {

int Init()
{
	const struct device *uac2 = DEVICE_DT_GET(DT_NODELABEL(uac2_synth));
	const struct device *udc = DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0));
	int err;

	if (!device_is_ready(udc)) {
		LOG_ERR("UDC not ready");
		return -ENODEV;
	}
	if (!device_is_ready(uac2)) {
		LOG_ERR("uac2 device not ready");
		return -ENODEV;
	}

	// Mandatory: uac2_init() fails with -EINVAL if ops are unset.
	usbd_uac2_set_ops(uac2, &kUac2Ops, NULL);

	err = usbd_add_descriptor(&twang_usbd, &twang_lang);
	if (err) {
		LOG_ERR("add lang descriptor: %d", err);
		return err;
	}
	err = usbd_add_descriptor(&twang_usbd, &twang_mfr);
	if (err) {
		LOG_ERR("add manufacturer descriptor: %d", err);
		return err;
	}
	err = usbd_add_descriptor(&twang_usbd, &twang_product);
	if (err) {
		LOG_ERR("add product descriptor: %d", err);
		return err;
	}

	err = usbd_add_configuration(&twang_usbd, USBD_SPEED_HS,
				     &twang_hs_config);
	if (err) {
		LOG_ERR("add HS configuration: %d", err);
		return err;
	}
	err = RegisterClasses(&twang_usbd, USBD_SPEED_HS);
	if (err) {
		return err;
	}

	err = usbd_add_configuration(&twang_usbd, USBD_SPEED_FS,
				     &twang_fs_config);
	if (err) {
		LOG_ERR("add FS configuration: %d", err);
		return err;
	}
	err = RegisterClasses(&twang_usbd, USBD_SPEED_FS);
	if (err) {
		return err;
	}

	// Multi-interface composite: the device code triple must advertise the
	// interface-association descriptor (misc class, common subclass, IAD).
	usbd_device_set_code_triple(&twang_usbd, USBD_SPEED_HS,
				    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	usbd_device_set_code_triple(&twang_usbd, USBD_SPEED_FS,
				    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	usbd_self_powered(&twang_usbd, true);

	err = usbd_init(&twang_usbd);
	if (err) {
		LOG_ERR("usbd_init: %d", err);
		return err;
	}

	err = usbd_enable(&twang_usbd);
	if (err) {
		LOG_ERR("usbd_enable: %d", err);
		return err;
	}

	LOG_INF("composite USB device enabled (UAC2 capture + MIDI 2.0)");
	return 0;
}

}  // namespace usb
