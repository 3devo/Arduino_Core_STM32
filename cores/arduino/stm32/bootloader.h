#ifndef _BOOTLOADER_H_
#define _BOOTLOADER_H_

/* Ensure DTR_TOGGLING_SEQ enabled */
#if defined(BL_LEGACY_LEAF) || defined(BL_HID)
  #ifndef DTR_TOGGLING_SEQ
    #define DTR_TOGGLING_SEQ
  #endif /* DTR_TOGGLING_SEQ || BL_HID */
#endif /* BL_LEGACY_LEAF */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Request to jump to system memory boot */
void jumpToBootloaderRequested(void);

/* Jump to system memory boot from user application */
void jumpToBootloaderIfRequested(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* _BOOTLOADER_H_ */
