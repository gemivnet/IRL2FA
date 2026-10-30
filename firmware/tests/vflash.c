/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "vflash.h"

#include <string.h>

void vflash_init(vflash_t *f)
{
    memset(f, 0, sizeof(*f));
    memset(f->mem, 0xff, sizeof(f->mem));
    f->cut_at = -1;
}

void vflash_cut(vflash_t *f, long at_op, vf_cut_t mode)
{
    f->cut_at = f->ops + at_op;
    f->mode = mode;
}

void vflash_power_on(vflash_t *f)
{
    f->dead = false;
    f->cut_at = -1;
}

void vflash_tear(vflash_t *f, uint32_t addr)
{
    f->torn[addr / 8] = 1;
}

static void noise(vflash_t *f, uint32_t addr, uint32_t len)
{
    uint32_t x = 0x9e3779b9u ^ (uint32_t)f->ops ^ addr;
    for (uint32_t i = 0; i < len; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        f->mem[addr + i] = (uint8_t)x;
    }
    for (uint32_t i = 0; i < len; i += 8) f->torn[(addr + i) / 8] = 1;
}

/* 1: go ahead, 0: power is off, -1: this is the operation being cut. */
static int op(vflash_t *f, uint32_t addr, uint32_t len)
{
    if (f->dead) return 0;
    if (addr < f->prot_hi && addr + len > f->prot_lo) f->violations++;
    long n = f->ops++;
    if (n == f->cut_at) {
        f->dead = true;
        return -1;
    }
    return 1;
}

static bool vf_read(void *ctx, uint32_t addr, void *buf, size_t len)
{
    const vflash_t *f = ctx;
    if (addr > VF_SIZE || len > VF_SIZE - addr) return false;
    memcpy(buf, f->mem + addr, len);
    bool ok = true;
    for (uint32_t a = addr & ~7u; a < addr + len; a += 8) {
        if (!f->torn[a / 8]) continue;
        ok = false;
        for (uint32_t i = 0; i < 8; i++)
            if (a + i >= addr && a + i < addr + len) ((uint8_t *)buf)[a + i - addr] = 0;
    }
    return ok;
}

static bool vf_erase(void *ctx, uint32_t addr)
{
    vflash_t *f = ctx;
    uint32_t page = addr & ~(VF_PAGE - 1u);
    if (addr >= VF_SIZE) return false;
    int go = op(f, page, VF_PAGE);
    if (go == 0) return false;
    if (go < 0) {
        if (f->mode == VF_CUT_HALF) {
            memset(f->mem + page, 0xff, VF_PAGE / 2);
            memset(f->torn + page / 8, 0, VF_PAGE / 16);
            f->torn[(page + VF_PAGE / 2) / 8] = 1;
        } else if (f->mode == VF_CUT_GARBAGE) {
            noise(f, page, VF_PAGE);
        }
        return false;
    }
    memset(f->mem + page, 0xff, VF_PAGE);
    memset(f->torn + page / 8, 0, VF_PAGE / 8);
    f->erases++;
    return true;
}

static bool vf_program(void *ctx, uint32_t addr, const void *buf, size_t len)
{
    vflash_t *f = ctx;
    if ((addr & 7u) || (len & 7u) || addr > VF_SIZE || len > VF_SIZE - addr) {
        f->violations++;
        return false;
    }
    int go = op(f, addr, (uint32_t)len);
    if (go == 0) return false;
    const uint8_t *src = buf;
    size_t done = go < 0 ? (f->mode == VF_CUT_HALF ? (len / 2) & ~(size_t)7 : 0) : len;
    for (size_t i = 0; i < done; i += 8) {
        static const uint8_t ERASED[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        if (f->torn[(addr + i) / 8] || memcmp(f->mem + addr + i, ERASED, 8) != 0) {
            f->violations++;
            return false;
        }
        memcpy(f->mem + addr + i, src + i, 8);
    }
    if (go < 0) {
        if (f->mode == VF_CUT_HALF && done < len) f->torn[(addr + done) / 8] = 1;
        else if (f->mode == VF_CUT_GARBAGE) noise(f, addr, (uint32_t)len);
        return false;
    }
    /* Read back, like the port's driver. */
    return memcmp(f->mem + addr, src, len) == 0;
}

fw_flash_t vflash_iface(vflash_t *f)
{
    fw_flash_t fl = {vf_read, vf_erase, vf_program, f, VF_PAGE};
    return fl;
}
