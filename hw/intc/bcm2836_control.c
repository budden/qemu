/*
 * Rasperry Pi 2 emulation ARM control logic module. At present, only
 * implements interrupt routing (not local timer or mailboxes).
 *
 * Ref: https://www.raspberrypi.org/documentation/hardware/raspberrypi/bcm2836/QA7_rev3.4.pdf
 *
 * Based on bcm2835_ic.c, terms below...
 */

/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

/* Heavily based on pl190.c, copyright terms below. */

/*
 * Arm PrimeCell PL190 Vector Interrupt Controller
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw/sysbus.h"

#define NCORES 4

#define ROUTE_CORE(x) ((x) & 0x3)
#define ROUTE_FIQ(x)  (((x) & 0x4) != 0)

#define IRQ_BIT(cntrl, num) (((cntrl) & (1 << (num))) != 0)
#define FIQ_BIT(cntrl, num) (((cntrl) & (1 << ((num) + 4))) != 0)

#define IRQ_CNTPSIRQ    0
#define IRQ_CNTPNSIRQ   1
#define IRQ_CNTHPIRQ    2
#define IRQ_CNTVIRQ     3
#define IRQ_MAILBOX0    4
#define IRQ_MAILBOX1    5
#define IRQ_MAILBOX2    6
#define IRQ_MAILBOX3    7
#define IRQ_GPU         8
#define IRQ_PMU         9
#define IRQ_AXI         10
#define IRQ_TIMER       11
#define IRQ_MAX         IRQ_TIMER

#define TYPE_BCM2836_CONTROL "bcm2836_control"
#define BCM2836_CONTROL(obj) OBJECT_CHECK(bcm2836_control_state, (obj), TYPE_BCM2836_CONTROL)

typedef struct bcm2836_control_state {
    SysBusDevice busdev;
    MemoryRegion iomem;

    // interrupt status registers
    // (not directly visible to user)
    bool gpu_irq, gpu_fiq;
    uint32_t localirqs[NCORES];

    // interrupt routing/control registers
    uint8_t route_gpu_irq, route_gpu_fiq;
    uint32_t timercontrol[NCORES];
    uint32_t mailboxcontrol[NCORES];

    // interrupt source registers, post-routing (visible)
    uint32_t irqsrc[NCORES];
    uint32_t fiqsrc[NCORES];
    
    // outputs to CPU cores
    qemu_irq irq[NCORES];
    qemu_irq fiq[NCORES];
} bcm2836_control_state;

/* Update interrupts.  */
static void bcm2836_control_update(bcm2836_control_state *s)
{
    int i, j;

    // reset pending IRQs/FIQs
    for (i = 0; i < NCORES; i++) {
        s->irqsrc[i] = s->fiqsrc[i] = 0;
    }

    //
    // step 1: apply routing logic, update status regs
    //
    
    if (s->gpu_irq) {
        assert(s->route_gpu_irq < NCORES);
        s->irqsrc[s->route_gpu_irq] |= (uint32_t)1 << IRQ_GPU;
    }

    if (s->gpu_fiq) {
        assert(s->route_gpu_fiq < NCORES);
        s->fiqsrc[s->route_gpu_fiq] |= (uint32_t)1 << IRQ_GPU;
    }

    for (i = 0; i < NCORES; i++) {
        if (s->localirqs[i]) {
            assert(s->localirqs[i] < (1 << IRQ_MAILBOX0));
            for (j = 0; j < IRQ_MAILBOX0; j++) {
                if ((s->localirqs[i] & (1 << j)) != 0) {
                    // local interrupt j is set
                    if (FIQ_BIT(s->timercontrol[i], j)) {
                        // deliver a FIQ
                        s->fiqsrc[i] |= (uint32_t)1 << j;
                    } else if (IRQ_BIT(s->timercontrol[i], j)) {
                        // deliver an IRQ
                        s->irqsrc[i] |= (uint32_t)1 << j;
                    } else {
                        // the interrupt is masked
                    }
                }
            }
        }
    }

    //
    // step 2: call set_irq appropriately for each output
    //
    
    for (i = 0; i < NCORES; i++) {
        qemu_set_irq(s->irq[i], s->irqsrc[i] != 0);
        qemu_set_irq(s->fiq[i], s->fiqsrc[i] != 0);
    }
}

// XXX: ugly kludge, because I can't seem to pass useful information in the "irq" parameter when using named interrupts
static void bcm2836_control_set_local_irq(void *opaque, int core, int local_irq, int level)
{
    bcm2836_control_state *s = (bcm2836_control_state *)opaque;

    assert(core >= 0 && core < NCORES);
    assert(local_irq >= 0 && local_irq <= IRQ_CNTVIRQ);

    if (level) {
        s->localirqs[core] |= 1 << local_irq;
    } else {
        s->localirqs[core] &= ~((uint32_t)1 << local_irq);
    }

    bcm2836_control_update(s);
}

static void bcm2836_control_set_local_irq0(void *opaque, int core, int level)
{
    bcm2836_control_set_local_irq(opaque, core, 0, level);
}

static void bcm2836_control_set_local_irq1(void *opaque, int core, int level)
{
    bcm2836_control_set_local_irq(opaque, core, 1, level);
}

static void bcm2836_control_set_local_irq2(void *opaque, int core, int level)
{
    bcm2836_control_set_local_irq(opaque, core, 2, level);
}

static void bcm2836_control_set_local_irq3(void *opaque, int core, int level)
{
    bcm2836_control_set_local_irq(opaque, core, 3, level);
}

static void bcm2836_control_set_gpu_irq(void *opaque, int irq, int level)
{
    bcm2836_control_state *s = (bcm2836_control_state *)opaque;

    s->gpu_irq = level;
    
    bcm2836_control_update(s);
}

static void bcm2836_control_set_gpu_fiq(void *opaque, int irq, int level)
{
    bcm2836_control_state *s = (bcm2836_control_state *)opaque;

    s->gpu_fiq = level;
    
    bcm2836_control_update(s);
}

static uint64_t bcm2836_control_read(void *opaque, hwaddr offset,
    unsigned size)
{
    bcm2836_control_state *s = (bcm2836_control_state *)opaque;
    uint32_t res = 0;

    switch (offset) {
        /* GPU interrupt routing */
    case 0xc:
        assert(s->route_gpu_fiq < NCORES && s->route_gpu_irq < NCORES);
        res = ((uint32_t)s->route_gpu_fiq << 2) | s->route_gpu_irq;
        break;
        
        /* Timer interrupt control registers */
    case 0x40:
        res = s->timercontrol[0];
        break;
    case 0x44:
        res = s->timercontrol[1];
        break;
    case 0x48:
        res = s->timercontrol[2];
        break;
    case 0x4c:
        res = s->timercontrol[3];
        break;

        /* Mailbox interrupt control registers */
    case 0x50:
        res = s->mailboxcontrol[0];
        break;
    case 0x54:
        res = s->mailboxcontrol[1];
        break;
    case 0x58:
        res = s->mailboxcontrol[2];
        break;
    case 0x5c:
        res = s->mailboxcontrol[3];
        break;

        /* IRQ source registers */
    case 0x60:
        res = s->irqsrc[0];
        break;
    case 0x64:
        res = s->irqsrc[1];
        break;
    case 0x68:
        res = s->irqsrc[2];
        break;
    case 0x6c:
        res = s->irqsrc[3];
        break;

        /* FIQ source registers */
    case 0x70:
        res = s->fiqsrc[0];
        break;
    case 0x74:
        res = s->fiqsrc[1];
        break;
    case 0x78:
        res = s->fiqsrc[2];
        break;
    case 0x7c:
        res = s->fiqsrc[3];
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2836_control_read: Bad offset %x\n", (int)offset);
        return 0;
    }

    return res;
}

static void bcm2836_control_write(void *opaque, hwaddr offset,
    uint64_t val, unsigned size)
{
    bcm2836_control_state *s = (bcm2836_control_state *)opaque;

    switch (offset) {
        /* GPU interrupt routing */
    case 0xc:
        s->route_gpu_irq = val & 0x3;
        s->route_gpu_fiq = (val >> 2) & 0x3;
        break;

        /* Timer interrupt control registers */
    case 0x40:
        s->timercontrol[0] = val & 0xff;
        break;
    case 0x44:
        s->timercontrol[1] = val & 0xff;
        break;
    case 0x48:
        s->timercontrol[2] = val & 0xff;
        break;
    case 0x4c:
        s->timercontrol[3] = val & 0xff;
        break;

        /* Mailbox interrupt control registers */
    case 0x50:
        s->mailboxcontrol[0] = val & 0xff;
        break;
    case 0x54:
        s->mailboxcontrol[0] = val & 0xff;
        break;
    case 0x58:
        s->mailboxcontrol[0] = val & 0xff;
        break;
    case 0x5c:
        s->mailboxcontrol[0] = val & 0xff;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2836_control_write: Bad offset %x\n", (int)offset);
        return;
    }
    bcm2836_control_update(s);
}

static const MemoryRegionOps bcm2836_control_ops = {
    .read = bcm2836_control_read,
    .write = bcm2836_control_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void bcm2836_control_reset(DeviceState *d)
{
    bcm2836_control_state *s = BCM2836_CONTROL(d);
    int i;

    s->route_gpu_irq = s->route_gpu_fiq = 0;

    for (i = 0; i < NCORES; i++) {
        s->timercontrol[i] = 0;
        s->mailboxcontrol[i] = 0;
    }
}

static int bcm2836_control_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    bcm2836_control_state *s = BCM2836_CONTROL(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2836_control_ops, s,
        TYPE_BCM2836_CONTROL, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);

    // inputs from each CPU core
    qdev_init_gpio_in_named(dev, bcm2836_control_set_local_irq0, "cntpsirq", NCORES);
    qdev_init_gpio_in_named(dev, bcm2836_control_set_local_irq1, "cntpnsirq", NCORES);
    qdev_init_gpio_in_named(dev, bcm2836_control_set_local_irq2, "cnthpirq", NCORES);
    qdev_init_gpio_in_named(dev, bcm2836_control_set_local_irq3, "cntvirq", NCORES);
    //qdev_init_gpio_in_named(dev, bcm2836_control_set_pmu_irq, "pmuirq", NCORES);

    // IRQ and FIQ inputs from upstream bcm2835 controller
    qdev_init_gpio_in_named(dev, bcm2836_control_set_gpu_irq, "gpu_irq", 1);
    qdev_init_gpio_in_named(dev, bcm2836_control_set_gpu_fiq, "gpu_fiq", 1);

    // outputs to CPU cores
    qdev_init_gpio_out_named(dev, s->irq, "irq", NCORES);
    qdev_init_gpio_out_named(dev, s->fiq, "fiq", NCORES);

    return 0;
}

static const VMStateDescription vmstate_bcm2836_control = {
    .name = TYPE_BCM2836_CONTROL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        // TODO
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2836_control_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = bcm2836_control_init;
    dc->reset = bcm2836_control_reset;
    dc->vmsd = &vmstate_bcm2836_control;
}

static TypeInfo bcm2836_control_info = {
    .name          = TYPE_BCM2836_CONTROL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(bcm2836_control_state),
    .class_init    = bcm2836_control_class_init,
};

static void bcm2836_control_register_types(void)
{
    type_register_static(&bcm2836_control_info);
}

type_init(bcm2836_control_register_types)
