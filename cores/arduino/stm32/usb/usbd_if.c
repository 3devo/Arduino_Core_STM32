/**
  ******************************************************************************
  * @file    usbd_if.c
  * @author  fpistm
  * @brief   USB Device common interface file
  ******************************************************************************
  */
#ifdef USBCON

#include "usbd_if.h"
#include "usbd_cdc_if.h"

#if !defined(USBD_REENUM_DISABLED)

/*
 * Below, support for re-attaching to USB is handled. USB-attachment is
 * detected by the presence of a pullup on one of the USB datalines.
 *
 * To force the USB host to re-enumerate, we must detach (disable
 * pullup), wait a bit, and re-attach (enable pullup). This can be done
 * while running, but must also happen at startup (including the detach
 * and delay, since USB might have been active just before reset, so we
 * must force a re-enumeration). On a cold boot (e.g. USB just plugged
 * in), the forced detach might not be needed, but will not be harmful
 * either.
 *
 * For pullup control, a number of cases are supported:
 *  1. An external pullup controllable through a GPIO (e.g. using an
 *     external transistor). The default output state (e.g. when the
 *     GPIO is floating) can be enabled or disabled, and the output
 *     polarity can be configured.
 *  2. A pullup built into the chip's USB core. This is automatically
 *     enabled by hardware or USB HAL code, and can be manually
 *     controlled using HAL functions.
 *  3. A fixed (always enabled) external pullup. To still allow detaching,
 *     a bit of a hack is used: By setting the D+ to OUTPUT LOW, it
 *     *looks* like the pullup is removed (the host has own, bigger,
 *     pulldown, to detect detaching, so writing LOW looks the same).
 *     This probably violates USB specifications, but does seem to work
 *     well in practice. It probably also should only be used briefly,
 *     keeping the pin OUTPUT LOW for longer might be problematic.
 *
 * These scenarios are considered in order: If the variant defines an
 * external pullup control pin, it is used, else if an internal pullup
 * is present, that is used, else the D+ trick is used.
 *
 * To configure #1, the variant should either define USBD_ATTACH_PIN and
 * USBD_ATTACH_LEVEL when the pullup is disabled by default and the pin
 * can be used to *attach*, or USBD_DETACH_PIN and USBD_DETACH_LEVEL
 * when the pullup is enabled by default and the pin can be used to
 * *detach*.
 */

/* Compatibility with the old way to specify this */
#if defined(USB_DISC_PIN) && !defined(USBD_ATTACH_PIN)
  #define USBD_ATTACH_PIN USB_DISC_PIN
  #define USBD_ATTACH_LEVEL LOW
  #warning "USB_DISC_PIN is deprecated, use USBD_ATTACH_PIN instead"
#endif /* defined(USB_DISC_PIN) && !defined(USBD_ATTACH_PIN) */

/* Some sanity checks */
#if defined(USBD_ATTACH_PIN) && defined(USBD_DETACH_PIN)
  #error "Cannot define both USBD_ATTACH_PIN and USBD_DETACH_PIN"
#endif /* defined(USBD_ATTACH_PIN) && defined(USBD_DETACH_PIN) */
#if defined(USBD_ATTACH_PIN) && !defined(USBD_ATTACH_LEVEL)
  #error "USBD_ATTACH_PIN also needs USBD_ATTACH_LEVEL defined"
#endif /* defined(USBD_ATTACH_PIN) && !defined(USBD_ATTACH_LEVEL) */
#if defined(USBD_DETACH_PIN) && !defined(USBD_DETACH_LEVEL)
  #error "USBD_DETACH_PIN also needs USBD_DETACH_LEVEL defined"
#endif /* defined(USBD_DETACH_PIN) && !defined(USBD_DETACH_LEVEL) */
#if (defined(USBD_DETACH_PIN) || defined(USBD_ATTACH_PIN)) && defined(USBD_FIXED_PULLUP)
  #error "Cannot define both USBD_FIXED_PULLUP and USBD_ATTACH_PIN or USBD_DETACH_PIN"
#endif /* (defined(USBD_DETACH_PIN) || defined(USBD_ATTACH_PIN)) && defined(USBD_FIXED_PULLUP) */

/* Either of these bits indicate that there are internal pullups */
#if defined(USB_BCDR_DPPU) || defined(USB_OTG_DCTL_SDIS)
  #define USBD_HAVE_INTERNAL_PULLUPS
#endif /* defined(USB_BCDR_DPPU) || defined(USB_OTG_DCTL_SDIS) */

/* Figure out which USB instance is used. This mirrors the decision made
 * in USBD_LL_Init in usbd_conf.c. */
#if defined(USE_USB_HS)
  #define USBD_USB_INSTANCE USB_OTG_HS
  #define USBD_DP_PINNAME USB_OTG_HS_DP
#elif defined(USB_OTG_FS)
  #define USBD_USB_INSTANCE USB_OTG_FS
  #define USBD_DP_PINNAME USB_OTG_FS_DP
#elif defined(USB)
  #define USBD_USB_INSTANCE USB
  #define USBD_DP_PINNAME USB_DP
#endif

/*
 * Translate pin number to a pin name (using the same define for both
 * attach and detach pins) and also define the complementary level. This
 * allows using the same code for the default-enabled and
 * default-disabled case as well as for the D+ trick (which does not
 * know the pin number, only the pin name).
 */
#if defined(USBD_ATTACH_PIN)
  #define USBD_PULLUP_CONTROL_PINNAME digitalPinToPinName(USBD_ATTACH_PIN)
  #define USBD_DETACH_LEVEL !(USBD_ATTACH_LEVEL)
#elif defined(USBD_DETACH_PIN)
  #define USBD_PULLUP_CONTROL_PINNAME digitalPinToPinName(USBD_DETACH_PIN)
  #define USBD_ATTACH_LEVEL !(USBD_DETACH_LEVEL)
#elif !defined(USBD_HAVE_INTERNAL_PULLUPS) || defined(USBD_FIXED_PULLUP)
  /* When no USB attach and detach pins were defined, and there are also
  * no internal pullups, assume there is a fixed external pullup and apply
  * the D+ trick. Also do this when there are internal *and* external
  * pulups (which is a hardware bug, but there are boards out there with
  * this). */
  #define USBD_PULLUP_CONTROL_PINNAME USBD_DP_PINNAME
  #define USBD_DETACH_LEVEL LOW
  // USBD_ATTACH_LEVEL not needed.
  #define USBD_DP_TRICK
#endif

 /**
  * @brief  USBD_early_startup_delay
  * @param  us - number of us to delay
  * @retval None
  *
  * This is a minimal delay which is usable in very early startup, when
  * nothing has been initialized yet (no clocks, no memory, no systick
  * timer). It works by counting CPU cycles, and assumes the system is
  * still running from the HSI.
  *
  * If the systick timer is already enabled, this assumes everything is
  * intialized and instead used the normal delayMicroseconds function.
  *
  * Max delay depends on HSI, but is around 268 sec with 16Mhz HSI.
  */
void USBD_early_startup_delay_us(uint32_t us) {
  if (SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) {
    delayMicroseconds(us);
    return;
  }

  #if !HSI_VALUE
  #error "Missing HSI_VALUE"
  #endif

  #if HSI_VALUE % 4000000 != 0
  #warning "HSI not multiple of 4MHz, early startup delay will be inaccurate!"
  #endif

  // To simplify this calculation, this assumes the HSI runs at a
  // multiple of 4Mhz (1Mhz to scale to us, times 4 to account for 4
  // cycles per loop).
  const uint32_t loops_per_us = (HSI_VALUE / 1000000) / 4;
  const uint32_t loop_count = us * loops_per_us;

  // Assembly loop, designed to run at exactly 4 cycles per loop.
  asm volatile (
    // Use the unified ARM/Thumb syntax, which seems to be more
    // universally used and corresponds to what avr-objdump outputs
    // See https://sourceware.org/binutils/docs/as/ARM_002dInstruction_002dSet.html
    ".syntax unified\n\t"
    "1:\n\t"
    "nop                         /* 1 cycle */\n\t"
    "subs %[loop], %[loop], #1    /* 1 cycle */\n\t"
    "bne  1b                     /* 2 if taken, 1 otherwise */\n\t"
    : : [loop] "l" (loop_count)
  );
}

/**
  * @brief  Force to re-enumerate USB.
  *
  * This is intended to be called at startup by core code. It could be
  * used at runtime, while USB is connected, to force re-enumeration
  * too, but that does not work in all cases (when USB is enabled on an
  * F103C8, setting output mode on the DP pin no longer has any effect).
  *
  * @param  None
  * @retval None
  */
WEAK void USBD_reenumerate(void)
{
#if defined(USBD_PULLUP_CONTROL_PINNAME)
  /* Detach */
  pin_function(USBD_PULLUP_CONTROL_PINNAME, STM_PIN_DATA(STM_MODE_OUTPUT_PP, GPIO_NOPULL, 0));
  digitalWriteFast(USBD_PULLUP_CONTROL_PINNAME, USBD_DETACH_LEVEL);

  /* Wait */
  USBD_early_startup_delay_us(USBD_ENUM_DELAY * 1000);

  /* Attach */
#if defined(USBD_DP_TRICK)
  /* Revert back to input (floating), needed for the D+ trick */
  pin_function(USBD_PULLUP_CONTROL_PINNAME, STM_PIN_DATA(STM_MODE_INPUT, GPIO_NOPULL, 0));
#else
  digitalWriteFast(USBD_PULLUP_CONTROL_PINNAME, USBD_ATTACH_LEVEL);
#endif /* defined(USBD_PULLUP_CONTROL_FLOATING) */
#elif defined(USBD_HAVE_INTERNAL_PULLUPS)
#ifdef USB_OTG_DCTL_SDIS
  uint32_t USBx_BASE = (uint32_t)USBD_USB_INSTANCE;
  USBx_DEVICE->DCTL |= USB_OTG_DCTL_SDIS;
  //USB_DevDisconnect(USBD_USB_INSTANCE);
  USBD_early_startup_delay_us(USBD_ENUM_DELAY * 1000);
  //USB_DevConnect(USBD_USB_INSTANCE);
  USBx_DEVICE->DCTL &= ~USB_OTG_DCTL_SDIS;
#else
  USBD_USB_INSTANCE->BCDR &= (uint16_t)(~(USB_BCDR_DPPU));
  USBD_early_startup_delay_us(USBD_ENUM_DELAY * 1000);
  USBD_USB_INSTANCE->BCDR |= (uint16_t)(USB_BCDR_DPPU);
#endif
#else
#warning "No USB attach/detach method, USB might not be reliable through system resets"
#endif
}

#else /* !defined(USBD_REENUM_DISABLED) */
WEAK void USBD_reenumerate(void) { }
#endif

#ifdef USBD_USE_CDC
void USBD_CDC_init(void)
{
  CDC_init();
}
#endif /* USBD_USE_CDC */
#endif /* USBCON */
