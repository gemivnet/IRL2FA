// Minimal Renode models of the STM32U0 RCC, PWR, FLASH interface, EXTI, RNG
// and ADC: enough register behaviour for the IRL2FA firmware (RM0503 register
// maps), nothing more. See firmware/port/stm32u0/README.md, "Emulation".
// SPDX-License-Identifier: AGPL-3.0-or-later
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Peripherals.Memory;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    // A register file whose reads can be post-processed (ready flags etc.).
    public abstract class U0RegisterFile : IDoubleWordPeripheral, IKnownSize
    {
        protected U0RegisterFile(long size) { Size = size; regs = new uint[size / 4]; }
        public long Size { get; }
        public virtual uint ReadDoubleWord(long offset) { return OnRead(offset, regs[offset / 4]); }
        public virtual void WriteDoubleWord(long offset, uint value) { regs[offset / 4] = OnWrite(offset, regs[offset / 4], value); }
        public virtual void Reset() { Array.Clear(regs, 0, regs.Length); OnReset(); }
        protected virtual uint OnRead(long offset, uint stored) { return stored; }
        protected virtual uint OnWrite(long offset, uint old, uint value) { return value; }
        protected virtual void OnReset() { }
        protected uint[] regs;
    }

    // RCC: every oscillator is ready as soon as it is enabled; SWS follows SW.
    public class STM32U0_RCC : U0RegisterFile
    {
        public STM32U0_RCC() : base(0x400) { Reset(); }
        protected override void OnReset() { regs[0] = 0x00000061; /* MSION, MSIRANGE 4 MHz */ }
        protected override uint OnRead(long offset, uint v)
        {
            switch(offset)
            {
            case 0x00: // CR: MSIRDY, HSIRDY, HSERDY, PLLRDY
                v &= ~((1u << 1) | (1u << 10) | (1u << 17) | (1u << 25));
                if((v & 1) != 0) v |= 1u << 1;
                if((v & (1u << 8)) != 0) v |= 1u << 10;
                if((v & (1u << 16)) != 0) v |= 1u << 17;
                if((v & (1u << 24)) != 0) v |= 1u << 25;
                return v;
            case 0x08: return (v & ~0x38u) | ((v & 7u) << 3); // CFGR: SWS = SW
            case 0x90: return (v & ~2u) | ((v & 1u) << 1);     // BDCR: LSERDY
            case 0x94: return (v & ~2u) | ((v & 1u) << 1);     // CSR: LSIRDY
            case 0x98: return (v & ~2u) | ((v & 1u) << 1);     // CRRCR: HSI48RDY
            default: return v;
            }
        }
    }

    // PWR: plain registers; SR2 reports the regulator settled (VOSF = REGLPF = 0).
    public class STM32U0_PWR : U0RegisterFile
    {
        public STM32U0_PWR() : base(0x400) { Reset(); }
        protected override void OnReset() { regs[0] = 0x00000200; /* VOS range 1 */ }
        protected override uint OnRead(long offset, uint v) { return offset == 0x14 ? 0u : v; }
        public uint LowPowerMode => regs[0] & 7u;
    }

    // FLASH interface: KEYR unlock sequence, LOCK, page erase (fills the page
    // with 0xFF), EOP/BSY. Programming is done by the CPU writing the flash
    // array directly, as on silicon; the model only counts PG sessions.
    public class STM32U0_FLASH : U0RegisterFile
    {
        public STM32U0_FLASH(MappedMemory flash, int pageSize = 2048) : base(0x400) { this.flash = flash; this.pageSize = pageSize; Reset(); }
        protected override void OnReset() { regs[0x14 / 4] = 0xC0000000; keyStage = 0; }
        public int PageErases { get; private set; }
        public int LockViolations { get; private set; }

        protected override uint OnWrite(long offset, uint old, uint value)
        {
            switch(offset)
            {
            case 0x08: // KEYR
                if(keyStage == 0 && value == 0x45670123) keyStage = 1;
                else if(keyStage == 1 && value == 0xCDEF89AB) { keyStage = 2; regs[0x14 / 4] &= ~(1u << 31); }
                else { keyStage = 3; LockViolations++; } // wrong sequence locks until reset
                return 0;
            case 0x10: // SR: write 1 to clear flags
                return old & ~value;
            case 0x14: // CR
                if((old & (1u << 31)) != 0)
                {
                    if(value != old) LockViolations++;
                    return old; // locked: ignored
                }
                if((value & (1u << 31)) != 0) { keyStage = 0; return value; }
                if((value & (1u << 16)) != 0 && (value & 2u) != 0) // STRT with PER
                {
                    var page = (int)((value >> 3) & 0x7F);
                    flash.SetRange((long)page * pageSize, pageSize, 0xFF);
                    PageErases++;
                    regs[0x10 / 4] |= 1u; // EOP
                    value &= ~(1u << 16);
                }
                return value;
            default:
                return value;
            }
        }

        private readonly MappedMemory flash;
        private readonly int pageSize;
        private int keyStage;
    }

    // EXTI with the U0 register map (RM0503): RTSR1, FTSR1, SWIER1, RPR1, FPR1,
    // EXTICR1..4 (port selection, 8 bits per line), IMR1. GPIO input n comes
    // from port n / 16 (A = 0, B = 1, ...), line n % 16. Outputs 0..2 are the
    // EXTI0_1, EXTI2_3 and EXTI4_15 NVIC lines.
    public class STM32U0_EXTI : IDoubleWordPeripheral, IKnownSize, IGPIOReceiver, INumberedGPIOOutput
    {
        public STM32U0_EXTI()
        {
            var c = new Dictionary<int, IGPIO>();
            for(int i = 0; i < 3; i++) c[i] = new GPIO();
            Connections = new ReadOnlyDictionary<int, IGPIO>(c);
            Reset();
        }
        public long Size => 0x400;
        public IReadOnlyDictionary<int, IGPIO> Connections { get; }
        public int EdgesSeen { get; private set; }

        public void OnGPIO(int number, bool value)
        {
            if(number < 0 || number >= levels.Length) return;
            var old = levels[number];
            levels[number] = value;
            if(old == value) return;
            int port = number / 16, line = number % 16;
            if(((exticr[line / 4] >> ((line % 4) * 8)) & 0xFF) != port) return;
            uint bit = 1u << line;
            if(value && (rtsr & bit) != 0) { rpr |= bit; EdgesSeen++; }
            if(!value && (ftsr & bit) != 0) { fpr |= bit; EdgesSeen++; }
            Update();
        }

        public uint ReadDoubleWord(long offset)
        {
            switch(offset)
            {
            case 0x00: return rtsr;
            case 0x04: return ftsr;
            case 0x0C: return rpr;
            case 0x10: return fpr;
            case 0x60: case 0x64: case 0x68: case 0x6C: return exticr[(offset - 0x60) / 4];
            case 0x80: return imr1;
            case 0x84: return emr1;
            case 0x90: return imr2;
            default: return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            switch(offset)
            {
            case 0x00: rtsr = value & 0xFFFF; break;
            case 0x04: ftsr = value & 0xFFFF; break;
            case 0x08: rpr |= value & 0xFFFF; break; // SWIER1
            case 0x0C: rpr &= ~value; break;
            case 0x10: fpr &= ~value; break;
            case 0x60: case 0x64: case 0x68: case 0x6C: exticr[(offset - 0x60) / 4] = value; break;
            case 0x80: imr1 = value; break;
            case 0x84: emr1 = value; break;
            case 0x90: imr2 = value; break;
            }
            Update();
        }

        public void Reset()
        {
            rtsr = ftsr = rpr = fpr = emr1 = 0;
            imr1 = 0xFFF80000; imr2 = 0x00000007; // direct lines enabled at reset
            Array.Clear(exticr, 0, 4);
            Update();
        }

        private void Update()
        {
            var p = (rpr | fpr) & imr1;
            Connections[0].Set((p & 0x0003) != 0);
            Connections[1].Set((p & 0x000C) != 0);
            Connections[2].Set((p & 0xFFF0) != 0);
        }

        private readonly bool[] levels = new bool[16 * 6];
        private readonly uint[] exticr = new uint[4];
        private uint rtsr, ftsr, rpr, fpr, imr1, emr1, imr2;
    }

    // RNG: CR (CONDRST self-clears), SR (DRDY while RNGEN), DR from a seeded
    // PRNG: give each emulated device its own Seed, or two devices would
    // generate the same keys.
    public class STM32U0_RNG : U0RegisterFile
    {
        public STM32U0_RNG(int seed = 1) : base(0x400) { Seed = seed; Reset(); }
        public int Seed { get => seed; set { seed = value; rng = new Random(value); } }
        public int WordsRead { get; private set; }
        protected override void OnReset() { regs[0] = 0x00800D00; rng = new Random(seed); }
        protected override uint OnWrite(long offset, uint old, uint value)
        {
            if(offset == 0x00) return value & ~(1u << 30); // CONDRST done at once
            if(offset == 0x04) return 0;
            return value;
        }
        protected override uint OnRead(long offset, uint v)
        {
            bool on = (regs[0] & 4u) != 0;
            if(offset == 0x04) return on ? 1u : 0u;
            if(offset == 0x08)
            {
                if(!on) return 0;
                WordsRead++;
                uint w;
                do { w = (uint)rng.Next(0x10000) << 16 | (uint)rng.Next(0x10000); } while(w == 0);
                return w;
            }
            return v;
        }
        private int seed;
        private Random rng;
    }

    // ADC: ADVREGEN/ADCAL/ADEN/ADSTART state machine, CHSELR (bit mode) with
    // CCRDY, single conversions of the lowest selected channel. Channel 12 is
    // VREFINT: VrefintMillivolts measured against VddaMillivolts.
    public class STM32U0_ADC : U0RegisterFile
    {
        public STM32U0_ADC() : base(0x400) { Reset(); }
        public decimal VddaMillivolts { get; set; } = 3000;
        public decimal VrefintMillivolts { get; set; } = 1212;
        public int Conversions { get; private set; }

        protected override uint OnWrite(long offset, uint old, uint value)
        {
            switch(offset)
            {
            case 0x00: return old & ~value; // ISR: write 1 to clear
            case 0x08: // CR
                value &= ~(1u << 31); // ADCAL completes at once
                if((value & 2u) != 0) value &= ~3u; // ADDIS
                else if((value & 1u) != 0 && (old & 1u) == 0) regs[0] |= 1u; // ADRDY
                if((value & 4u) != 0 && (value & 1u) != 0) // ADSTART
                {
                    regs[0x40 / 4] = Convert();
                    regs[0] |= (1u << 2) | (1u << 3); // EOC, EOS
                    value &= ~4u;
                }
                return value;
            case 0x28: regs[0] |= 1u << 13; return value; // CHSELR -> CCRDY
            default: return value;
            }
        }

        private uint Convert()
        {
            Conversions++;
            uint sel = regs[0x28 / 4] & 0x7FFFF;
            int ch = 0;
            while(ch < 19 && (sel & (1u << ch)) == 0) ch++;
            decimal mv = ch == 12 ? VrefintMillivolts : 0;
            var raw = (int)Math.Round(mv / VddaMillivolts * 4095);
            return (uint)Math.Max(0, Math.Min(4095, raw));
        }
    }
}
