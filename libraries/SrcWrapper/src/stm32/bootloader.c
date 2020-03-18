#include "bootloader.h"

#include "stm32_def.h"
#include "backup.h"
#include "stm32yyxx_ll_system.h"
#include "usbd_if.h"

/*
 * STM32 built-in bootloader in system memory support
 */

static bool BootIntoBootloaderAfterReset;

/* Request to jump to system memory boot */
WEAK void jumpToBootloaderRequested(void)
{
  BootIntoBootloaderAfterReset = true;
  NVIC_SystemReset();
}

// This overrides the Reset_Handler that is run on reset before
// *anything* else (including memory initialization). Only the stack
// pointer is set up by this time.
void Reset_Handler() {
  // Jump to the bootloader if needed.
  jumpToBootloader();

  // Continue with regular startup by calling the original reset handler
  Original_Reset_Handler();
}

/* Jump to system memory boot from user application */
WEAK void jumpToBootloader(void)
{
  // Boot into bootloader if BootIntoBootloaderAfterReset is set.
  // Note that BootIntoBootloaderAfterReset is a noinit variable, so it
  // s not automatically initialized on startup (so it can keep its
  // value across resets). At initial poweron, its value can be
  // *anything*, so only consider its value after a software reset. In
  // all cases, clear its value (this both takes care of giving it an
  // initial value after power-up, and prevents booting into the
  // bootloader more than once for a single request).
  bool doBootloader = __HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) && BootIntoBootloaderAfterReset;
  BootIntoBootloaderAfterReset = false;

  if (doBootloader) {
#ifdef USBCON
    USBD_reenumerate();
#endif
    void (*sysMemBootJump)(void);

    __HAL_RCC_CLEAR_RESET_FLAGS();

#ifdef __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH
    /* Remap system Flash memory at address 0x00000000 */
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
    uint32_t sysMem_addr = 0;
#elif defined(STM32F1xx) && defined ((STM32F101xG) || defined (STM32F103xG))
    // From AN2606, table 136 "Bootloader device-dependent parameters"
    // STM32F10xxx XL-density, aka 768K-1M flash, aka F and G flash size codes
    uint32_t sysMem_addr = 0x1FFFE000;
#elif defined(STM32F1xx) && defined (STM32F105xC) || defined (STM32F107xC)
    // STM32F105xx/107xx from AN2606, table 136 "Bootloader device-dependent parameters"
    uint32_t sysMem_addr = 0x1FFFB000;
#elif defined (STM32F100xB) || defined (STM32F100xE) || defined (STM32F101x6) || \
    defined (STM32F101xB) || defined (STM32F101xE) ||  defined (STM32F102x6) || \
    defined (STM32F102xB) || defined (STM32F103x6) || defined (STM32F103xB) || \
    defined (STM32F103xE)
    // STM32F10xxx from AN2606, table 136 "Bootloader device-dependent parameters"
    // This does not check for STM32F1xx, to prevent catching
    // STM32F105xx/STM32F107xx or XL-density chips that are introduced later.
    // Defines from system/Drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f1xx.h
    uint32_t sysMem_addr = 0x1FFFF000;
#elif defined(STM32F7xx) || defined(STM32H7xx)
    // From AN2606, table 136 "Bootloader device-dependent parameters"
    // TODO: Reference manual for has a different value...
    uint32_t sysMem_addr = 0x1FF00000;
#else
    #error "System flash address unknown for this CPU"
#endif

    /**
     * Set jump memory location for system memory
     * Use address with 4 bytes offset which specifies jump location
     * where program starts
     */
    sysMemBootJump = (void (*)(void))(*((uint32_t *)(sysMem_addr + 4)));

    /**
     * Set main stack pointer.
     * This step must be done last otherwise local variables in this function
     * don't have proper value since stack pointer is located on different position
     *
     * Set direct address location which specifies stack pointer in SRAM location
     */
    __set_MSP(*(uint32_t *)sysMem_addr);

    /**
     * Jump to set location
     * This will start system memory execution
     */
    sysMemBootJump();

    while (1);
  }
}

/*
 * Legacy maple bootloader support
 */
#ifdef BL_LEGACY_LEAF
void dtr_togglingHook(uint8_t *buf, uint32_t *len)
{
  /**
   * Four byte is the magic pack "1EAF" that puts the MCU into bootloader.
   * Check if the incoming contains the string "1EAF".
   * If yes, put the MCU into the bootloader mode.
   */
  if ((*len >= 4) && (buf[0] == '1') && (buf[1] == 'E') && (buf[2] == 'A') && (buf[3] == 'F')) {
    NVIC_SystemReset();
  }
}
#endif /* BL_LEGACY_LEAF */

/*
 * HID bootloader support
 */
#ifdef BL_HID
void dtr_togglingHook(uint8_t *buf, uint32_t *len)
{
  /**
   * Four byte is the magic pack "1EAF" that puts the MCU into bootloader.
   * Check if the incoming contains the string "1EAF".
   * If yes, put the MCU into the bootloader mode.
   */
  if ((*len >= 4) && (buf[0] == '1') && (buf[1] == 'E') && (buf[2] == 'A') && (buf[3] == 'F')) {
    enableBackupDomain();
    /* New HID Bootloader (ver 2.2+) */
    setBackupRegister(HID_MAGIC_NUMBER_BKP_INDEX, HID_MAGIC_NUMBER_BKP_VALUE);
#ifdef HID_OLD_MAGIC_NUMBER_BKP_INDEX
    /* Compatibility to the old HID Bootloader (ver <= 2.1) */
    setBackupRegister(HID_OLD_MAGIC_NUMBER_BKP_INDEX, HID_MAGIC_NUMBER_BKP_VALUE);
#endif
    NVIC_SystemReset();
  }
}
#endif /* BL_HID */
