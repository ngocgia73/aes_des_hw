#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/bitops.h>
#include <linux/random.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <linux/synclink.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>

#include "aes_des_hw.h"
#include <mach/fmem.h>
#include "frammap_if.h"


#define ALLOC_DMA_BUF_CACHED
#define SUPPORT_HOST_CANCEL

#define FIRST_MINOR                 0
#define NUMBER_OF_DEVICE_NUMBER     1
#define DEVICE_NAME                 "aes_des"

// the number of device file will be connect to struct cdev
#define COUNT                       1
#define DEVICE_FILE_NAME            aes_des_hw

#define SIZE_1K             (1  << 10)
#define SIZE_64K            (64 << 10)
#define SIZE_1M             (1  << 20)
#define SIZE_2M             (2  << 20)
#define SIZE_4M             (4  << 20)

static dev_t    dev_num;
static struct device  *_device = NULL;
static struct class   *cls = NULL;
static struct cdev    *my_cdev = NULL;


static struct aes_des_data
{
    void *dma_addr_va; // virtual address
    dma_addr_t *dma_addr_pa; // physical address
    int dma_map_size;
    int share_mem_size;
};

void __iomem *aes_base_addr_va = NULL;
struct resource *irq; = NULL;
sttic wait_queue_head_t aes_wq;

static volatile int dma_int_ok;
static struct semaphore  sema;
static unsigned int exit_for_cancel = 0;

// function to read from register

static inline int aaes_read_reg(int offset)
{
    return ioread32(aes_base_addr_va + offset);
}

// function to write to register
static inline void aes_write_reg(int offset, int value)
{
    iowrite32(value, aes_base_addr_va + offset);
}

static irqreturn_t aes_hw_irq_handler(int irq, void *devid)
{
    int status;
    status = aaes_read_reg(SEC_MaskedIntrStatus);
    if ((status & Data_done) != 0) 
    {
        dma_int_ok = 1;
    }
    aes_write_reg(SEC_ClearIntrStatus, Data_done);

    if (exit_for_cancel == 1) 
    {
        exit_for_cancel = 0;
        up(&sema);
    }
    else 
    {
#ifdef SUPPORT_HOST_CANCEL
        wake_up_interruptible(&aes_wq);
#else
        wake_up(&aes_wq);
#endif
    }

    return IRQ_HANDLED;
}

static int alloc_share_mem(struct aes_des_data *p_data, int size)
{
    if(!p_data)
    {
        printk(KERN_ERR "invalid input\n");
        return -EINVAL;
    }

#ifdef ALLOC_DMA_BUF_CACHED
    struct page *p_page = NULL;
    //2nd : how many page need to allocate memory
    p_page = alloc_pages(GFP_KERNEL, get_order(size)); // maximum is 4M
    if(!p_page)
    {
        return -ENOMEM;
    }
    p_data->dma_addr_va =  page_address(p_page); // get virtual addr
    p_data->dma_addr_pa = page_to_phys(p_page); // get physical addr
    p_data->share_mem_size = size;

#else
    p_data->dma_addr_va = dma_alloc_coherent(NULL, SIZE_64K, &(p_data->dma_addr_pa), GFP_DMA | GFP_KERNEL);
    if(!p_data->dma_addr_va)
    {
        return -ENOMEM;
    }
    p_data->share_mem_size = SIZE_64K;
#endif
    return 0;
}

static void free_share_mem(struct aes_des_data *p_data)
{
    if(!p_data)
    {
        printk(KERN_ERR "invalid input\n");
        return -EINVAL;
    }

    if(p_data->dma_addr_va)
    {
#ifdef ALLOC_DMA_BUF_CACHED
        // free page
        free_pages((unsigned long)p_data->dma_addr_va, get_order(p_data->share_mem_size));
#else
        dma_free_coherent(NULL, p_data->share_mem_size, p_data->dma_addr_va, p_data->dma_addr_pa);
#endif
        p_data->dma_addr_va = NULL;
    }
}

static int es_hw_open(struct inode *inode, struct file *filp)
{
    // @TODO: allocate memory which will be to user space
    struct aes_des_data *p_data = kzalloc(sizeof(struct aes_des_data), GFP_KERNEL);
    if(!aes_des_data)
    {
        printk(KERN_ERR "unable to allocate mem for aes_des_data\n");
        return -ENOMEM;
    }
    
    retval = alloc_share_mem(p_data);
    if(retval < 0)
    {
        printk(KERN_ERR "unable to allocate share mem\n");
    }
    file->private_data = p_data;
    return 0;
}

static int es_hw_release(struct inode *inode, struct file *filp)
{
    // @TODO: free memory which was shared to user space 
    struct aes_des_data *p_data = (struct aes_des_data *)filp->private_data;
    if(!p_data)
        return -EINVAL;
    // free member of struct aes_des_data
    free_share_mem(p_data);

    kfree(p_data);
    p_data = NULL;
    filp->private_data = NULL;
    return 0;
}

static int es_hw_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int retval = -1;
    int share_mem_size = 0;
    unsigned long pfn, offset = 0;
    struct aes_des_data *p_data = filp->private_data;
    if(!p_data)
    {
        return retval;
    }
    p_data->dma_map_size = vma->vm_end - vma->vm_start;

    // check input from user space
    if(!(vma->vm_flags & VM_WRITE))
    {
        PRINTK(KERN_ERR "app bug: PROT_WRITE please\n");
        return -EINVAL;
    }

    if(!(vma->vm_flags & VM_SHARED))
    {
        printk(KERN_ERR "app bug: MAP_SHARED please \n");
        return -EINVAL;
    }

    if(!(p_data->dma_addr_pa))
    {
        retval = alloc_share_mem(p_data, SIZE_4M);
        if(ret < 0)
        {
            printk(KERN_ERR "unable to allocate share mem\n");
            return -ENOMEM; 
        }
    }
    share_mem_size = p_data->share_mem_size;
    
    // indicate the offset from the start hardware address
    offset = vma->vm_pgoff << PAGE_SHIFT;
    if(offset > share_mem_size)
    {
        free_share_mem(p_data);
        return -EINVAL;
    }
    if(p_data->dma_map_size > (share_mem_size - offset))
    {
        free_share_mem(p_data);
        return -EFAULT;
    }

#ifndef ALLOC_DMA_BUF_CACHED
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif
    // avoid to swap out this vma
    // VERY IMPORTANT
    vma->vm_flags |= VM_RESERVED;

    // identify pfn
    pfn = vir_to_phys(p_data->dma_addr_va + offset) >> PAGE_SHIFT;

    // proceed to remap mem
    if(remap_pfn_range(vma, vma->vm_start, pfn, p_data->dma_map_size), vma->vm_page_prot)
    {
        printk(KERN_ERR "remap mem failed\n");
        free_share_mem(p_data);
        return -EFAULT;
    }
    
    return 0;
}

static int calc_key_length(esreq * srq, unsigned int* p_key_info)
{
    int ret = 0;
    int algorithm = srq->algorithm;

    switch (algorithm) {
        case Algorithm_DES:
            srq->key_length = 8;
            srq->IV_length  = 8;
            break;

        case Algorithm_Triple_DES:
            srq->key_length = 24;
            srq->IV_length  = 8;
            break;

        case Algorithm_AES_128:
            srq->key_length = 16;
            srq->IV_length  = 16;
            break;

        case Algorithm_AES_192:
            srq->key_length = 24;
            srq->IV_length  = 16;
            break;

        case Algorithm_AES_256:
            srq->key_length = 32;
            srq->IV_length  = 16;
            break;

        default:
            ret = -EINVAL;
            break;
    }

    *p_key_info = (srq->key_length << 16) | (srq->IV_length);

    return ret;
}

static void IV_Output(int addr)
{
    int i;

    for (i = 0; i < 4; i++) 
    {
        *(u32 *) (addr + 4 * i) = aes_read_reg(SEC_LAST_IV0 + 4 * i);
      //printk("IV = 0x%08x\n",*(u32 *)(addr + 4 * i));
    }
}

static int getkey(int algorithm, u32 key_addr, u32 IV_addr, u32 data)
{
    int i;
    int key_length;
    int IV_length;

    key_length = data >> 16;
    IV_length  = data & 0xFFFF;

#ifdef INTERNAL_DEBUG
    *(unsigned int *)(key_addr)      = 0x603deb10;
    *(unsigned int *)(key_addr + 4)  = 0x15ca71be;
    *(unsigned int *)(key_addr + 8)  = 0x2b73aef0;
    *(unsigned int *)(key_addr + 12) = 0x857d7781;
    *(unsigned int *)(key_addr + 16) = 0x1f352c07;
    *(unsigned int *)(key_addr + 20) = 0x3b6108d7;
    *(unsigned int *)(key_addr + 24) = 0x2d9810a3;
    *(unsigned int *)(key_addr + 28) = 0x0914dff4;

    *(unsigned int *)(IV_addr)       = 0x00010203;
    *(unsigned int *)(IV_addr + 4)   = 0x04050607;
    *(unsigned int *)(IV_addr + 8)   = 0x08090a0b;
    *(unsigned int *)(IV_addr + 12)  = 0x0c0d0e0f;
#else
    for (i = 0; i < key_length / 4; i++)
        *(unsigned int *)(key_addr + i * 4) = random();

    for (i = 0; i < IV_length / 4; i++)
        *(unsigned int *)(IV_addr + i * 4) = random();
#endif

    return 0;
}


int es_wait_event_on(void)
{
    int ret = -1;

#ifdef SUPPORT_HOST_CANCEL
    ret = wait_event_interruptible_timeout
    (
        aes_wq,
        dma_int_ok != 0,
        msecs_to_jiffies(5000) /* 5 seconds */
    );
#else
    ret = wait_event_timeout
    (
        aes_wq,
        dma_int_ok != 0,
        msecs_to_jiffies(5000) /* 5 seconds */
    );
#endif

    /* normal result */
    if ( ret > 0) 
    {
        ret = 0;
        if (!dma_int_ok) 
        {
            panic("%s dma_int_ok not ok%d", __func__, dma_int_ok);
        }
    }
    else if (ret == -ERESTARTSYS) 
    {
        exit_for_cancel = 1;
    }
    else if (ret == 0) 
    {
        /* timeout */
        printk(KERN_ERR "%s, wait event timeout %d!!!! \n", __func__, ret);
        ret = -ETIMEDOUT;
    }
    else 
    {
        /* unexpected situation */
        panic("* %s unexpected situation %d %d", __func__, dma_int_ok, ret);
    }

    dma_int_ok = 0;
    return ret;
}


static int es_do_dma(int stage, esreq * srq, struct sec_file_data* p_data )
{
    int i, ret = 0;
    unsigned int section;
    unsigned int block_size    = 1;    /* block size of CFB mode of both AES and DES is 1 block */
    unsigned int data_src_addr = es_va2pa(srq->DataIn_addr);
    unsigned int data_dst_addr = es_va2pa(srq->DataOut_addr);
    unsigned int data_length   = srq->data_length;
    int  algorithm             = srq->algorithm;
    int  mode                  = srq->mode;

    if(data_length <= 16) {
        printk("data length=0x%08x not valid!\n", data_length);
        return -EINVAL;
    }

#ifdef ALLOC_DMA_BUF_CACHED
    /* clean and invalidate data cache to memory */
    fmem_dcache_sync((void *)srq->DataIn_addr,  data_length, DMA_BIDIRECTIONAL);
    fmem_dcache_sync((void *)srq->DataOut_addr, data_length, DMA_BIDIRECTIONAL);
#endif

    data_length -= 16;      //sub IV return length

    if (mode != CFB_mode) {
        switch (algorithm) {
            case Algorithm_DES:
            case Algorithm_Triple_DES:
                block_size = 8; /* except for CFB mode, block size of DES is 8 bytes */
                break;

            case Algorithm_AES_128:
            case Algorithm_AES_192:
            case Algorithm_AES_256:
                block_size = 16; /* except for CFB mode, block size of AES is 16 bytes */
                break;

            default:
                printk("* wrong alg 0x%08X \n", algorithm);
                return -EINVAL;
        }
    }

    if ((data_length % block_size) != 0) {
        printk("data length not block_size(%d) aligned\n", block_size);
        return -EINVAL;
    }

    section = MAX_SEC_DMATrasSize/block_size;
    section = section*block_size;

    down(&sema);

    aes_write_reg(SEC_FIFOThreshold, (1 << 8) + 1);
    aes_write_reg(SEC_IntrEnable, Data_done);

    /* start to set registers */
    /* 1. Set EncryptControl register */
    if (mode == ECB_mode)
        aes_write_reg(SEC_EncryptControl, algorithm | mode | stage);
    else
        aes_write_reg(SEC_EncryptControl, First_block | algorithm | mode | stage);

    /* 2. Set Initial vector IV */
    aes_write_reg(SEC_DESIVH, *(u32 *) srq->IV_addr);
    aes_write_reg(SEC_DESIVL, *(u32 *) (srq->IV_addr + 1));
    aes_write_reg(SEC_AESIV2, *(u32 *) (srq->IV_addr + 2));
    aes_write_reg(SEC_AESIV3, *(u32 *) (srq->IV_addr + 3));

    /* 3. Set Key value */
    for (i = 0; i < 8; i++)
        aes_write_reg(SEC_DESKey1H + 4 * i, *(u32 *) (srq->key_addr + i));

    /* 5. Set DMA related register
     *    hw will add section size to dma addr,
     *    so you don't have update it in loop
     *    Nish comment.
     */
    aes_write_reg(SEC_DMASrc, data_src_addr);
    aes_write_reg(SEC_DMADes, data_dst_addr);

    while (data_length) {
        if (data_length >= section) {
            aes_write_reg(SEC_DMATrasSize, section);
            data_length -= section;
        }
        else {
            aes_write_reg(SEC_DMATrasSize, data_length);
            data_length = 0;
        }

        //6. Set DmaEn bit of DMAStatus to 1 to active DMA engine
        dma_int_ok = 0;
        aes_write_reg(SEC_DMACtrl, DMA_Enable);
        //7. Wait transfer size is complete
        ret = es_wait_event_on();
        if (ret == -ERESTARTSYS) 
        {
            printk(KERN_ERR "* es_wait_event get cancel singal! \n");
            break;
        } 
        else if (ret != 0) 
        {
            printk(KERN_ERR "* es_wait_event wrong %d \n", ret);
            break;
        }
    }

    up(&sema);

    return ret;
}


static long es_hw_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    esreq srq;
    int status = -1;
    int key_info = 0;
    struct aes_des_data* p_data = filp->private_data;
    int es_func = 0;

    status = copy_from_user(&srq, (void __user *)arg, sizeof(esreq));
    if (status != 0) {
        printk(KERN_ERR "copy_from_user failure\n");
        goto exit_err;
    }


    switch (cmd) {
        case ES_GETKEY:
            status = calc_key_length(&srq, &key_info);
            if (status != 0) {
                goto exit_err;
            }

            status = getkey(srq.algorithm, (u32) srq.key_addr, (u32) srq.IV_addr, key_info);
            if (status != 0) {
                goto exit_err;
            }

            status = copy_to_user((void __user *)arg, &srq, sizeof(esreq));
            if (status != 0)
                goto exit_err;

            break;

        case ES_ENCRYPT:
        case ES_AUTO_ENCRYPT:
        case ES_DECRYPT:
        case ES_AUTO_DECRYPT:
            status = calc_key_length(&srq, &key_info);
            if (status != 0) 
            {
                goto exit_err;
            }

            if ((cmd == ES_AUTO_ENCRYPT)) 
            {
                status = getkey(srq.algorithm, (u32) srq.key_addr, (u32) srq.IV_addr, key_info);
                if (status != 0) 
                {
                    goto exit_err;
                }
            }


            if ((cmd == ES_AUTO_DECRYPT) || cmd == (ES_DECRYPT)) 
            {
                es_func = Decrypt_Stage;
            }
            else 
            {
                es_func = Encrypt_Stage;
            }

            status = es_do_dma(es_func, &srq, p_data);
            if (status)
                goto exit_err;

            IV_Output((int)DMA_INFO_VA(p_data) + srq.data_length - 16);
            break;

        default:
            printk(KERN_ERR "* ioctl wrong cmd 0x%08X \n", cmd);
            status = -ENOIOCTLCMD;
            goto exit_err;
    }
exit_err:

    /* cancel action due to signal, we cannot release semaphore due to hardware still running.
     * if the interrupt rises, it will release the semaphore to service another users
     */
    if (status) 
    {
        printk(KERN_ERR "%s, failure! \n", __func__);
    }
    return status;

}

static unsigned int es_hw_poll(struct file *filp, struct poll_table_struct *pwait)
{
    unsigned int mask;
    mask |= POLLIN | POLLRDNORM;
    return mask;
}

static size_t es_hw_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    return count;
}

static size_t es_hw_write(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    return count;
}

static struct file_operation es_hw_fops = {
    owner:THIS_MODULE,
    unlocked_ioctl:es_hw_ioctl,
    mmap:es_hw_mmap,
    open:es_hw_open,
    release:es_hw_release,
    read:es_hw_read,
    write:es_hw_write,
    poll:es_hw_poll,
};

static int es_hw_probe(struct platform_device *pdev)
{
    int retval = -1;
    struct resource *mem = NULL;
    
    // create device number
    retval = alloc_chrdev_region(&dev_num, FIRST_MINOR, NUMBER_OF_DEVICE_NUMBER, DEVICE_NAME);
    if(retval < 0)
    {
        printk(KERN_ERR "alloc_chrdev_region failed : ret = %d\n", retval);
        goto  __FAILED_CREATE_DEVNUM;
    }

    // create class of device in sys/class
    cls = class_create(THIS_MODULE , CLASS_NAME);
    if(IS_ERR(cls))
    {
        printk(KERN_ERR "class_create failed\n");
        goto __FAILED_CREATE_CLASS_DEVICE;
    }

    // create device file
    _device = device_create(cls, NULL, dev_num, NULL, DEVICE_FILE_NAME%d, MINOR(dev_num));
    if(IS_ERR(_device))
    {
        printk(KERN_ERR "failed to create device file\n");
        goto __FAILED_CREATE_DEVICE_FILE;
    }

    // allocate mem for struct cdev
    my_cdev = cdev_alloc();
    if(IS_ERR(my_cdev))
    {
        printk(KERN_ERR "cdev_alloc failed\n");
        goto __FAILED_CDEV_ALLOC;
    }

    // init the field of struct cdev
    cdev_init(my_cdev, &es_hw_fops);
    
    // register struct cdev to linux kernel
    // create connection bw struct cdev and device file
    // @COUNT : the number of device file will be connect to struct cdev

    retval = cdev_add(my_cdev, dev_num, COUNT);
    if(retval < 0)
    {
        printk(KERN_ERR "failed to cdev_add\n");
        goto __FAILED_CDEV_ADD;
    }

    // get resource 
    mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    (!mem)
    {
        printk(KERN_ERR "no mem resource\n");
        retval = -ENODEV;
        goto __FAILED_GET_RESOURCE_MEM;
    }

    // mapping physical addr to kernel virtual address
    aes_base_addr_va = (void __iomem *)ioremap_nocache(mem->start, mem->end - mem->start + 1);
    if(!aes_base_addr_va) 
    {
        printk(KERN_ERR "ioremap failed\n");
        retval = EFAULT;
        goto __FAILED_IOREMAP;    
    }

    irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
    (!irq)
    {
        printk(KERN_ERR "no irq resource\n");
        retval = -ENODEV;
        goto __FAILED_GET_RESOURCE_IRQ;
    }
    
    retval = request_irq(irq->start, aes_hw_irq_handler, NULL, dev_name(&pdev->dev), pdev);
    if(retval)
    {
        printk(KERN_ERR "request_irq failed\n");
        goto __FAILED_REQUEST_IRQ;
    }

    // init wait queue
    init_waitqueue_head(&aes_wq);

    // init semaphore
    sema_init(&sema, 1);
    dma_int_ok = 0;

    return retval;

__FAILED_REQUEST_IRQ:

__FAILED_IOREMAP:

__FAILED_GET_RESOURCE_IRQ:
    iounmap((void __iomem *)aes_base_addr_va);

__FAILED_GET_RESOURCE_MEM:

__FAILED_CDEV_ADD:
    cdev_del(my_cdev);
__FAILED_CDEV_ALLOC:
    device_destroy(cls, dev_num);
__FAILED_CREATE_DEVICE_FILE:
    class_destroy(cls);
__FAILED_CREATE_CLASS_DEVICE:
    unregister_chrdev_region(dev_num, NUMBER_OF_DEVICE_NUMBER);
__FAILED_CREATE_DEVNUM:
    return retval;

}

static int __devexit es_hw_remove(struct platform_device *pdev)
{
    cdev_del(my_cdev);
    device_destroy(cls, dev_num);
    class_destroy(cls);
    unregister_chrdev_region(dev_num, NUMBER_OF_DEVICE_NUMBER);
    ioremap((void __iomem *)aes_base_addr_va);
    free_irq(irq->start, NULL);
    return 0;
}

static struct platform_driver es_hw_driver = {
    .driver = {
            .owner = THIS_MODULE,
            .name  = DEVICE_NAME,
    },
    .probe = es_hw_probe,
    .remove = __devexit_p(es_hw_remove),
}


static void es_hw_device_release(struct  device *dev)
{
    return;
}

static struct resource es_hw_resource[] = 
{
    [0] = {
            .start = AES_PA_BASE,
            .end   = AES_PA_LIMIT,
            .flags = IORESOURCE_MEM, 
    },
    [1] = {
            .start = AES_0_IRQ,
            .end   = AES_0_IRQ,
            .flags = IORESOURCE_IRQ,
    }
};

static struct platform_device es_hw_device = {
    .name   =   DEVICE_NAME,
    .id     =   -1,
    .num_resources  = ARRAY_SIZE(es_hw_resource),
    .resource       = es_hw_resource,
    .dev    = {
        .dma_mask   = &es_hw_do_dmamask,
        .coherent_dma_mask = 0xFFFFFFFF,
        .release = es_hw_device_release,
    },
};

int es_fd = 0;
static pmuReg_t security_pmu[] = {
    {0xB4, (0x1 << 10), (0x1 << 10), 0, 0},     //turn on/off clock
    {0xA0, (0x1 << 17), (0x1 << 17), 0, 0}      //reset
};

pmuRegInfo_t security_pmu_info = {
    MODULE_NAME,
    ARRAY_SIZE(security_pmu),
    ATTR_TYPE_NONE,
    security_pmu
};

/*
 * Platform init function. This platform dependent.
 */
int platform_pmu_init(void)
{
    es_fd = ftpmu010_register_reg(&security_pmu_info);
    if (es_fd < 0) {
        printk("Error in regster PMU \n");
        return -1;
    }
    ftpmu010_write_reg(es_fd, 0xB4, 0, 1 << 10);        // turn on AES clock
    ftpmu010_write_reg(es_fd, 0xA0, 1 << 17, 1 << 17);  // release AES

    return 0;
}

/*
 * Platform exit function. This platform dependent.
 */
int platform_pmu_exit(void)
{
    ftpmu010_write_reg(es_fd, 0xB4, 1 << 10, 1 << 10);  // turn off AES clock
    ftpmu010_deregister_reg(es_fd);

    return 0;
}
static int __init ae_hw_init(void)
{
    // init clock related 
    platform_pmu_init();
    platform_device_register(&es_hw_device);
    platform_driver_register(&es_hw_driver);
    return 0;
}

static void __exit es_hw_exit(void)
{
    // release clock related
    platform_pmu_exit();
    platform_driver_unregister(&es_hw_driver);
    platform_device_unregister(&es_hw_device);
}

module_init(es_hw_init);
module_exit(es_hw_exit);


MODULE_DESCRIPTION("security driver");
MODULE_AUTHOR("Daniel Nguyen <daniel.nguyen0105@gmail.com>");
MODULE_LICENSE("GPL");
