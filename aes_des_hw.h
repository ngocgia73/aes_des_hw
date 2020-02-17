#ifndef _AES_DES_HW_H_
#define _AES_DES_HW_H_

#include <linux/ioctl.h>

/**
 * @brief security algorithm argument and address
 */
typedef struct esreq_tag {
    int          algorithm;     ///< security algorithm, such as Algorithm_DES/Algorithm_Triple_DES/Algorithm_AES_128/Algorithm_AES_192/Algorithm_AES_256
    int          mode;          ///< security mode, such as ECB_mode/CBC_mode/CTR_mode/CFB_mode/OFB_mode

    unsigned int data_length;   ///< size of data, max value = 4096, include 16 bytes IV
    unsigned int key_length;    ///< size of key
    unsigned int IV_length;     ///< size of Initial Vector

    unsigned int key_addr[8];   ///< key array, it maybe be modify by security algorithm
    unsigned int IV_addr[4];    ///< Initial Vector array, it maybe be modify by security algorithm

    unsigned int DataIn_addr;   ///< input data address point
    unsigned int DataOut_addr;  ///< output data address point

    unsigned int Key_backup;    ///< backup key
    unsigned int IV_backup;     ///< backup Initial Vector
} esreq;

/* security mode */
#define ECB_mode			    0x00
#define CBC_mode			    0x10
#define CTR_mode			    0x20
#define CFB_mode			    0x40
#define OFB_mode			    0x50

/* security algorithm */
#define Algorithm_DES			0x0
#define Algorithm_Triple_DES	0x2
#define Algorithm_AES_128		0x8
#define Algorithm_AES_192		0xA
#define Algorithm_AES_256		0xC

/* Use 'e' as magic number */
#define IOC_MAGIC  'e'

/**
 * \b ioctl(security_fd, ES_GETKEY, &tag)
 *
 * \arg get random key and Initial Vector
 * \arg parameter :
 * \n \b \e pointer \b \e tag : argument from user space ioctl parameter, it means structure esreq
 */
#define ES_GETKEY           _IOWR(IOC_MAGIC, 8, esreq)
/**
 * \b ioctl(security_fd, ES_GETKEY, &tag)
 *
 * \arg encrypt data, must set key and Initial Vector first
 * \arg parameter :
 * \n \b \e pointer \b \e tag : argument from user space ioctl parameter, it means structure esreq
 */
#define ES_ENCRYPT          _IOWR(IOC_MAGIC, 9, esreq)
/**
 * \b ioctl(security_fd, ES_GETKEY, &tag)
 *
 * \arg decrypt data, must set key and Initial Vector first
 * \arg parameter :
 * \n \b \e pointer \b \e tag : argument from user space ioctl parameter, it means structure esreq
 */
#define ES_DECRYPT          _IOWR(IOC_MAGIC, 10, esreq)
/**
 * \b ioctl(security_fd, ES_AUTO_ENCRYPT, &tag)
 *
 * \arg auto encrypt data, key and Initial Vector will be auto generated
 * \arg parameter :
 * \n \b \e pointer \b \e tag : argument from user space ioctl parameter, it means structure esreq
 */
#define ES_AUTO_ENCRYPT    	_IOWR(IOC_MAGIC, 11, esreq)
/**
 * \b ioctl(security_fd, ES_AUTO_DECRYPT, &tag)
 *
 * \arg auto decrypt data, key and Initial Vector will be auto generated
 * \arg parameter :
 * \n \b \e pointer \b \e tag : argument from user space ioctl parameter, it means structure esreq
 */
#define ES_AUTO_DECRYPT    	_IOWR(IOC_MAGIC, 12, esreq)



#define MODULE_NAME             "security"

/* register */
#define SEC_EncryptControl      0x00000000
#define SEC_SlaveRunEnable      0x00000004
#define SEC_FIFOStatus          0x00000008
#define SEC_PErrStatus          0x0000000C
#define SEC_DESKey1H            0x00000010
#define SEC_DESKey1L            0x00000014
#define SEC_DESKey2H            0x00000018
#define SEC_DESKey2L            0x0000001C
#define SEC_DESKey3H            0x00000020
#define SEC_DESKey3L            0x00000024
#define SEC_AESKey6             0x00000028
#define SEC_AESKey7             0x0000002C
#define SEC_DESIVH              0x00000030
#define SEC_DESIVL              0x00000034
#define SEC_AESIV2              0x00000038
#define SEC_AESIV3              0x0000003C
#define SEC_INFIFOPort          0x00000040
#define SEC_OutFIFOPort         0x00000044
#define SEC_DMASrc              0x00000048
#define SEC_DMADes              0x0000004C
#define SEC_DMATrasSize         0x00000050
#define SEC_DMACtrl             0x00000054
#define SEC_FIFOThreshold       0x00000058
#define SEC_IntrEnable          0x0000005C
#define SEC_IntrStatus          0x00000060
#define SEC_MaskedIntrStatus    0x00000064
#define SEC_ClearIntrStatus     0x00000068
#define SEC_LAST_IV0            0x00000080
#define SEC_LAST_IV1            0x00000084
#define SEC_LAST_IV2            0x00000088
#define SEC_LAST_IV3            0x0000008c

#if defined(CONFIG_PLATFORM_GM8129) || defined(CONFIG_PLATFORM_GM8287) || defined(CONFIG_PLATFORM_GM8136)
#define MAX_SEC_DMATrasSize ((((4<<30)-1)>>2)<<2)   ///< 4 Alignment
#else
#define MAX_SEC_DMATrasSize ((((4<<10)-1)>>2)<<2)   ///< 4 Alignment
#endif

/* bit mapping of command register */
//SEC_EncryptControl
#define Parity_check        0x100
#define First_block         0x80

//SEC_DMACtrl
#define DMA_Enable          0x1

//SEC_IntrStatus
#define Data_done           0x1

#define Decrypt_Stage       0x1
#define Encrypt_Stage       0x0


#endif /* _AES_DES_HW_H_ */
