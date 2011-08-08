/*
 * QEMU ISA MM VGA Emulator.
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw.h"
#include "console.h"
#include "pc.h"
#include "vga_int.h"
#include "pixel_ops.h"
#include "qemu-timer.h"
#include "exec-memory.h"

typedef struct ISAVGAMMState {
    VGACommonState vga;
    int it_shift;
} ISAVGAMMState;

/* Memory mapped interface */
static uint32_t vga_mm_readb (void *opaque, target_phys_addr_t addr)
{
    ISAVGAMMState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift) & 0xff;
}

static void vga_mm_writeb (void *opaque,
                           target_phys_addr_t addr, uint32_t value)
{
    ISAVGAMMState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift, value & 0xff);
}

static uint32_t vga_mm_readw (void *opaque, target_phys_addr_t addr)
{
    ISAVGAMMState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift) & 0xffff;
}

static void vga_mm_writew (void *opaque,
                           target_phys_addr_t addr, uint32_t value)
{
    ISAVGAMMState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift, value & 0xffff);
}

static uint32_t vga_mm_readl (void *opaque, target_phys_addr_t addr)
{
    ISAVGAMMState *s = opaque;

    return vga_ioport_read(&s->vga, addr >> s->it_shift);
}

static void vga_mm_writel (void *opaque,
                           target_phys_addr_t addr, uint32_t value)
{
    ISAVGAMMState *s = opaque;

    vga_ioport_write(&s->vga, addr >> s->it_shift, value);
}

static const MemoryRegionOps vga_mm_ctrl_ops = {
    .old_mmio = {
        .read = {
            vga_mm_readb,
            vga_mm_readw,
            vga_mm_readl,
        },
        .write = {
            vga_mm_writeb,
            vga_mm_writew,
            vga_mm_writel,
        },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void vga_mm_init(ISAVGAMMState *s, target_phys_addr_t vram_base,
                        target_phys_addr_t ctrl_base, int it_shift)
{
    MemoryRegion *s_ioport_ctrl, *vga_io_memory;

    s->it_shift = it_shift;
    s_ioport_ctrl = qemu_malloc(sizeof(*s_ioport_ctrl));
    memory_region_init_io(s_ioport_ctrl, &vga_mm_ctrl_ops, s,
                          "vga-mm-ctrl", 0x100000);

    vga_io_memory = qemu_malloc(sizeof(*vga_io_memory));
    /* XXX: endianness? */
    memory_region_init_io(vga_io_memory, &vga_mem_ops, &s->vga,
                          "vga-mem", 0x20000);

    vmstate_register(NULL, 0, &vmstate_vga_common, s);

    memory_region_add_subregion(get_system_memory(), ctrl_base, s_ioport_ctrl);
    s->vga.bank_offset = 0;
    memory_region_add_subregion(get_system_memory(),
                                vram_base + 0x000a0000, vga_io_memory);
    memory_region_set_coalescing(vga_io_memory);
}

int isa_vga_mm_init(target_phys_addr_t vram_base,
                    target_phys_addr_t ctrl_base, int it_shift)
{
    ISAVGAMMState *s;

    s = qemu_mallocz(sizeof(*s));

    vga_common_init(&s->vga, VGA_RAM_SIZE);
    vga_mm_init(s, vram_base, ctrl_base, it_shift);

    s->vga.ds = graphic_console_init(s->vga.update, s->vga.invalidate,
                                     s->vga.screen_dump, s->vga.text_update, s);

    vga_init_vbe(&s->vga);
    return 0;
}
