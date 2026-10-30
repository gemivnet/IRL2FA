// Minimal Renode models of the STM32U0 RCC, PWR, FLASH interface, EXTI, RNG
// and ADC: enough register behaviour for the IRL2FA firmware (RM0503 register
// maps), nothing more. See firmware/port/stm32u0/README.md, "Emulation".
//
// Every register offset, bit and base address the models use is a constant in
// U0Map below, named after ST's CMSIS header (stm32u083xx.h): OFF_<P>_<REG> is
// offsetof(<P>_TypeDef, <REG>), any other name is the header macro of that
// name. firmware/tools/check_hw_constants.py compiles them against the
// vendored header and fails on any difference, so a model cannot quietly
// disagree with the silicon the firmware is written for.
// SPDX-License-Identifier: AGPL-3.0-or-later
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Peripherals.CPU;
using Antmicro.Renode.Peripherals.IRQControllers;
using Antmicro.Renode.Peripherals.Memory;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    // Checked against stm32u083xx.h by firmware/tools/check_hw_constants.py.
    public static class U0Map
    {
        // ---- base addresses ----
        public const uint FLASH_BASE = 0x08000000;
        public const uint FLASH_R_BASE = 0x40022000;
        public const uint RCC_BASE = 0x40021000;
        public const uint PWR_BASE = 0x40007000;
        public const uint EXTI_BASE = 0x40021800;
        public const uint RNG_BASE = 0x40025000;
        public const uint ADC1_BASE = 0x40012400;
        public const uint I2C1_BASE = 0x40005400;
        public const uint SPI1_BASE = 0x40013000;
        public const uint USART1_BASE = 0x40013800;
        public const uint GPIOA_BASE = 0x50000000;
        public const uint GPIOB_BASE = 0x50000400;
        public const uint GPIOC_BASE = 0x50000800;
        public const uint GPIOF_BASE = 0x50001400;

        // ---- RCC ----
        public const long OFF_RCC_CR = 0x00;
        public const long OFF_RCC_CFGR = 0x08;
        public const long OFF_RCC_BDCR = 0x90;
        public const long OFF_RCC_CSR = 0x94;
        public const long OFF_RCC_CRRCR = 0x98;
        public const uint RCC_CR_MSION = 1u << 0;
        public const uint RCC_CR_MSIRDY = 1u << 1;
        public const uint RCC_CR_HSION = 1u << 8;
        public const uint RCC_CR_HSIRDY = 1u << 10;
        public const uint RCC_CR_HSEON = 1u << 16;
        public const uint RCC_CR_HSERDY = 1u << 17;
        public const uint RCC_CR_PLLON = 1u << 24;
        public const uint RCC_CR_PLLRDY = 1u << 25;
        public const uint RCC_CFGR_SW = 7u << 0;
        public const uint RCC_CFGR_SWS = 7u << 3;
        public const uint RCC_BDCR_LSEON = 1u << 0;
        public const uint RCC_BDCR_LSERDY = 1u << 1;
        public const uint RCC_CSR_LSION = 1u << 0;
        public const uint RCC_CSR_LSIRDY = 1u << 1;
        public const uint RCC_CSR_RMVF = 1u << 23;
        public const uint RCC_CSR_OBLRSTF = 1u << 25;
        public const uint RCC_CSR_PINRSTF = 1u << 26;
        public const uint RCC_CSR_PWRRSTF = 1u << 27;
        public const uint RCC_CSR_SFTRSTF = 1u << 28;
        public const uint RCC_CRRCR_HSI48ON = 1u << 0;
        public const uint RCC_CRRCR_HSI48RDY = 1u << 1;

        // ---- PWR ----
        public const long OFF_PWR_CR1 = 0x00;
        public const long OFF_PWR_SR2 = 0x14;
        public const uint PWR_CR1_LPMS = 7u << 0;
        public const uint PWR_CR1_LPMS_1 = 2u << 0;
        public const uint PWR_CR1_LPR = 1u << 14;
        public const uint PWR_CR1_VOS_0 = 1u << 9;

        // ---- FLASH ----
        public const long OFF_FLASH_ACR = 0x00;
        public const long OFF_FLASH_KEYR = 0x08;
        public const long OFF_FLASH_SR = 0x10;
        public const long OFF_FLASH_CR = 0x14;
        public const long OFF_FLASH_ECCR = 0x18;
        public const long OFF_FLASH_OPTKEYR = 0x0C;
        public const long OFF_FLASH_OPTR = 0x20;
        public const uint FLASH_CR_PG = 1u << 0;
        public const uint FLASH_CR_OPTSTRT = 1u << 17;
        public const uint FLASH_CR_OBL_LAUNCH = 1u << 27;
        public const uint FLASH_CR_OPTLOCK = 1u << 30;
        public const uint FLASH_OPTR_RDP = 0xFFu;
        public const uint FLASH_SR_EOP = 1u << 0;
        public const uint FLASH_CR_PER = 1u << 1;
        public const int FLASH_CR_PNB_Pos = 3;
        public const uint FLASH_CR_PNB = 0x7Fu << 3;
        public const uint FLASH_CR_STRT = 1u << 16;
        public const uint FLASH_CR_LOCK = 1u << 31;
        public const uint FLASH_ECCR_ECCD = 1u << 31;
        public const uint FLASH_ECCR_ECCC = 1u << 30;
        public const uint FLASH_ECCR_ECCCIE = 1u << 24;
        public const uint FLASH_ECCR_SYSF_ECC = 1u << 20;
        public const uint FLASH_ECCR_ADDR_ECC = 0x3FFFu;
        public const uint FlashKey1 = 0x45670123; // RM0503 3.3.5, not in the CMSIS header
        public const uint FlashKey2 = 0xCDEF89AB;
        public const uint OptKey1 = 0x08192A3B; // RM0503 3.4.2
        public const uint OptKey2 = 0x4C5D6E7F;

        // ---- EXTI ----
        public const long OFF_EXTI_RTSR1 = 0x00;
        public const long OFF_EXTI_FTSR1 = 0x04;
        public const long OFF_EXTI_SWIER1 = 0x08;
        public const long OFF_EXTI_RPR1 = 0x0C;
        public const long OFF_EXTI_FPR1 = 0x10;
        public const long OFF_EXTI_EXTICR = 0x60;
        public const long OFF_EXTI_IMR1 = 0x80;
        public const long OFF_EXTI_EMR1 = 0x84;
        public const long OFF_EXTI_IMR2 = 0x90;

        // ---- RNG ----
        public const long OFF_RNG_CR = 0x00;
        public const long OFF_RNG_SR = 0x04;
        public const long OFF_RNG_DR = 0x08;
        public const long OFF_RNG_HTCR = 0x10;
        public const uint RNG_CR_RNGEN = 1u << 2;
        public const uint RNG_CR_CONDRST = 1u << 30;
        public const uint RNG_CR_CONFIGLOCK = 1u << 31;
        public const uint RNG_SR_DRDY = 1u << 0;

        // ---- ADC ----
        public const long OFF_ADC_ISR = 0x00;
        public const long OFF_ADC_CR = 0x08;
        public const long OFF_ADC_CHSELR = 0x28;
        public const long OFF_ADC_DR = 0x40;
        public const uint ADC_ISR_ADRDY = 1u << 0;
        public const uint ADC_ISR_EOC = 1u << 2;
        public const uint ADC_ISR_EOS = 1u << 3;
        public const uint ADC_ISR_CCRDY = 1u << 13;
        public const uint ADC_CR_ADEN = 1u << 0;
        public const uint ADC_CR_ADDIS = 1u << 1;
        public const uint ADC_CR_ADSTART = 1u << 2;
        public const uint ADC_CR_ADCAL = 1u << 31;

        // ---- peripherals the Stop 2 audit looks at ----
        public const long OFF_I2C_CR1 = 0x00;
        public const uint I2C_CR1_PE = 1u << 0;
        public const long OFF_SPI_CR1 = 0x00;
        public const uint SPI_CR1_SPE = 1u << 6;
        public const long OFF_USART_CR1 = 0x00;
        public const uint USART_CR1_UE = 1u << 0;
        public const long OFF_GPIO_MODER = 0x00;
        public const long OFF_GPIO_OSPEEDR = 0x08;
        public const long OFF_GPIO_PUPDR = 0x0C;
        public const long OFF_GPIO_IDR = 0x10;

        // Cortex-M0+ System Control Register (Armv6-M ARM B3.2.7), not in the
        // device header: SLEEPDEEP is bit 2.
        public const ulong ScbScr = 0xE000ED10;
        public const uint ScbScrSleepDeep = 1u << 2;
    }

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
    // CSR's reset flags (RM0503 5.4.27) live outside the register reset:
    // a new machine has had a power-on (PWRRSTF | PINRSTF), a machine reset
    // is a software reset (SFTRSTF), PowerOnReset() is a cell put in, and
    // writing RMVF clears them.
    public class STM32U0_RCC : U0RegisterFile
    {
        public STM32U0_RCC() : base(0x400) { Reset(); resetFlags = U0Map.RCC_CSR_PWRRSTF | U0Map.RCC_CSR_PINRSTF; }
        protected override void OnReset()
        {
            regs[U0Map.OFF_RCC_CR / 4] = 0x00000061; /* MSION, MSIRANGE 4 MHz */
            if(powerOnPending) { resetFlags |= U0Map.RCC_CSR_PWRRSTF | U0Map.RCC_CSR_PINRSTF; powerOnPending = false; }
            else resetFlags |= U0Map.RCC_CSR_SFTRSTF;
        }
        public void PowerOnReset() { powerOnPending = true; }
        public void OptionByteReset() { resetFlags |= U0Map.RCC_CSR_OBLRSTF; }
        public uint ResetFlags => resetFlags;
        protected override uint OnWrite(long offset, uint old, uint value)
        {
            if(offset == U0Map.OFF_RCC_CSR && (value & U0Map.RCC_CSR_RMVF) != 0) resetFlags = 0;
            return offset == U0Map.OFF_RCC_CSR ? value & ~U0Map.RCC_CSR_RMVF : value;
        }
        private uint resetFlags;
        private bool powerOnPending;
        protected override uint OnRead(long offset, uint v)
        {
            switch(offset)
            {
            case U0Map.OFF_RCC_CR:
                v &= ~(U0Map.RCC_CR_MSIRDY | U0Map.RCC_CR_HSIRDY | U0Map.RCC_CR_HSERDY | U0Map.RCC_CR_PLLRDY);
                if((v & U0Map.RCC_CR_MSION) != 0) v |= U0Map.RCC_CR_MSIRDY;
                if((v & U0Map.RCC_CR_HSION) != 0) v |= U0Map.RCC_CR_HSIRDY;
                if((v & U0Map.RCC_CR_HSEON) != 0) v |= U0Map.RCC_CR_HSERDY;
                if((v & U0Map.RCC_CR_PLLON) != 0) v |= U0Map.RCC_CR_PLLRDY;
                return v;
            case U0Map.OFF_RCC_CFGR: return (v & ~U0Map.RCC_CFGR_SWS) | ((v & U0Map.RCC_CFGR_SW) << 3);
            case U0Map.OFF_RCC_BDCR: return Ready(v, U0Map.RCC_BDCR_LSEON, U0Map.RCC_BDCR_LSERDY);
            case U0Map.OFF_RCC_CSR: return Ready(v, U0Map.RCC_CSR_LSION, U0Map.RCC_CSR_LSIRDY) | resetFlags;
            case U0Map.OFF_RCC_CRRCR: return Ready(v, U0Map.RCC_CRRCR_HSI48ON, U0Map.RCC_CRRCR_HSI48RDY);
            default: return v;
            }
        }
        private static uint Ready(uint v, uint on, uint rdy) { return (v & ~rdy) | ((v & on) != 0 ? rdy : 0); }
    }

    // PWR: plain registers; SR2 reports the regulator settled (VOSF = REGLPF = 0).
    //
    // Stop 2 entry audit (RM0503 4.3.8, 33.5): each time the core executes
    // WFI with SLEEPDEEP set and LPMS = Stop 2, check what real silicon
    // requires but the emulated core would not notice:
    //  - no EXTI pending bit set (else the Stop 2 entry is ignored);
    //  - LPR clear (else Stop 1 is entered instead);
    //  - the peripherals that cannot run in Stop 2 are disabled: I2C1 (only
    //    I2C3 runs in Stop 2), SPI1, USART1, ADC, RNG;
    //  - no GPIO left as a floating digital input (input mode, no pull),
    //    which draws current on real silicon.
    // Violations are counted (Stop2Violations) and the last one is named
    // (LastStop2Violation); the tests require zero.
    public class STM32U0_PWR : U0RegisterFile
    {
        public STM32U0_PWR(IMachine machine, TranslationCPU cpu) : base(0x400)
        {
            this.machine = machine;
            cpu.AddHookAtWfiStateChange(OnWfi);
            Reset();
        }
        protected override void OnReset() { regs[U0Map.OFF_PWR_CR1 / 4] = 0x00000208; /* RM0503 4.4.1: VOS range 1, FPD_STOP */ }
        protected override uint OnRead(long offset, uint v) { return offset == U0Map.OFF_PWR_SR2 ? 0u : v; }
        public uint LowPowerMode => regs[U0Map.OFF_PWR_CR1 / 4] & U0Map.PWR_CR1_LPMS;
        public int Stop2Entries { get; private set; }
        public int Stop2Violations { get; private set; }
        public string LastStop2Violation { get; private set; } = "none";

        private void OnWfi(bool entering)
        {
            if(!entering) return;
            var bus = machine.SystemBus;
            if((bus.ReadDoubleWord(U0Map.ScbScr) & U0Map.ScbScrSleepDeep) == 0) return;
            var cr1 = regs[U0Map.OFF_PWR_CR1 / 4];
            if((cr1 & U0Map.PWR_CR1_LPMS) != U0Map.PWR_CR1_LPMS_1) return;
            Stop2Entries++;
            var problems = new List<string>();
            if((cr1 & U0Map.PWR_CR1_LPR) != 0) problems.Add("PWR_CR1.LPR set");
            var pending = bus.ReadDoubleWord(U0Map.EXTI_BASE + U0Map.OFF_EXTI_RPR1) | bus.ReadDoubleWord(U0Map.EXTI_BASE + U0Map.OFF_EXTI_FPR1);
            if(pending != 0) problems.Add(string.Format("EXTI pending 0x{0:X}", pending));
            Check(problems, "I2C1", U0Map.I2C1_BASE + U0Map.OFF_I2C_CR1, U0Map.I2C_CR1_PE);
            Check(problems, "SPI1", U0Map.SPI1_BASE + U0Map.OFF_SPI_CR1, U0Map.SPI_CR1_SPE);
            Check(problems, "USART1", U0Map.USART1_BASE + U0Map.OFF_USART_CR1, U0Map.USART_CR1_UE);
            Check(problems, "ADC", U0Map.ADC1_BASE + U0Map.OFF_ADC_CR, U0Map.ADC_CR_ADEN);
            Check(problems, "RNG", U0Map.RNG_BASE + U0Map.OFF_RNG_CR, U0Map.RNG_CR_RNGEN);
            foreach(var port in new[] { Tuple.Create('A', U0Map.GPIOA_BASE), Tuple.Create('B', U0Map.GPIOB_BASE),
                                        Tuple.Create('C', U0Map.GPIOC_BASE), Tuple.Create('F', U0Map.GPIOF_BASE) })
            {
                var moder = bus.ReadDoubleWord((ulong)port.Item2 + U0Map.OFF_GPIO_MODER);
                var pupdr = bus.ReadDoubleWord((ulong)port.Item2 + U0Map.OFF_GPIO_PUPDR);
                for(int pin = 0; pin < 16; pin++)
                    if(((moder >> (2 * pin)) & 3) == 0 && ((pupdr >> (2 * pin)) & 3) == 0)
                        problems.Add(string.Format("P{0}{1} floating input", port.Item1, pin));
            }
            if(problems.Count == 0) return;
            Stop2Violations++;
            LastStop2Violation = string.Join(", ", problems);
            this.Log(LogLevel.Warning, "Stop 2 entry: {0}", LastStop2Violation);
        }

        private void Check(List<string> problems, string name, ulong address, uint enable)
        {
            if((machine.SystemBus.ReadDoubleWord(address) & enable) != 0) problems.Add(name + " enabled");
        }

        private readonly IMachine machine;
    }

    // FLASH interface: KEYR unlock sequence, LOCK, page erase (fills the page
    // with 0xFF), EOP/BSY. Programming is done by the CPU writing the flash
    // array directly, as on silicon; the model only counts PG sessions.
    //
    // ECC (RM0503 3.3.2, 3.3.5 caution): InjectEccError(address) marks the
    // double-word holding `address` as having inconsistent ECC bits, which is
    // what a program or erase cut by a power loss leaves behind. Every CPU
    // read of it then sets FLASH_ECCR.ECCD and raises an NMI, as on silicon,
    // until the page is erased. The marks survive a machine reset (they are
    // in the flash array).
    //
    // Option bytes (RM0503 3.4): FLASH_OPTR reads the loaded value; with
    // OPTLOCK cleared (OPTKEYR sequence, after the flash unlock) a write
    // sets the value OPTSTRT programs, and OBL_LAUNCH loads it and resets
    // the machine (OBLRSTF). A new chip ships unprotected (RDP = 0xAA).
    // RdpLevel and OptionBytePrograms are for the tests; Rcc names the RCC
    // model for the reset flag.
    //
    // Power cuts: TearLastOperation() leaves what a cell pulled during the
    // latest flash operation would: the page erased last, if nothing was
    // programmed into it since, half erased with an ECC fault in the rest;
    // else the double-word programmed last into it with an ECC fault.
    public class STM32U0_FLASH : U0RegisterFile
    {
        public STM32U0_FLASH(IMachine machine, MappedMemory flash, NVIC nvic, STM32U0_RCC rcc = null, int pageSize = 2048) : base(0x400)
        {
            this.machine = machine;
            Rcc = rcc;
            this.flash = flash;
            this.nvic = nvic;
            this.pageSize = pageSize;
            Reset();
        }
        protected override void OnReset()
        {
            regs[U0Map.OFF_FLASH_CR / 4] = 0xC0000000; keyStage = 0; optKeyStage = 0; needRearm = true;
            regs[U0Map.OFF_FLASH_OPTR / 4] = optrLoaded;
        }
        public STM32U0_RCC Rcc { get; }
        public int RdpLevel => (optrLoaded & U0Map.FLASH_OPTR_RDP) == 0xAA ? 0 : (optrLoaded & U0Map.FLASH_OPTR_RDP) == 0xCC ? 2 : 1;
        public uint Optr { get => optrLoaded; set { optrLoaded = optrProgrammed = value; regs[U0Map.OFF_FLASH_OPTR / 4] = value; } }
        public int OptionBytePrograms { get; private set; }
        public int ProgramOps { get; private set; }

        // SHA-256 (hex) of flash bytes [offset, offset + length): the tests
        // compare the record store before and after an update.
        public string RegionHash(long offset, long length)
        {
            var bytes = new byte[length];
            for(long i = 0; i < length; i += 4)
            {
                var w = flash.ReadDoubleWord(offset + i);
                for(int k = 0; k < 4 && i + k < length; k++) bytes[i + k] = (byte)(w >> (8 * k));
            }
            using(var sha = System.Security.Cryptography.SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "");
        }

        public string TearLastOperation()
        {
            if(lastErasedPage < 0) return "nothing to tear";
            long baseOff = (long)lastErasedPage * pageSize;
            ulong pageAddr = U0Map.FLASH_BASE + (ulong)baseOff;
            if(!programmedSinceErase)
            {
                // An erase cut half way: the second half still holds noise.
                var rnd = new Random(lastErasedPage);
                for(long i = pageSize / 2; i < pageSize; i++) flash.WriteByte(baseOff + i, (byte)rnd.Next(256));
                InjectEccError(pageAddr + (ulong)pageSize / 2);
                return $"erase of page {lastErasedPage} torn";
            }
            // The last double-word programmed: the highest non-erased one.
            for(long off = pageSize - 8; off >= 0; off -= 8)
            {
                if(flash.ReadDoubleWord(baseOff + off) != 0xFFFFFFFF || flash.ReadDoubleWord(baseOff + off + 4) != 0xFFFFFFFF)
                {
                    InjectEccError(pageAddr + (ulong)off);
                    return $"program at 0x{pageAddr + (ulong)off:X} torn";
                }
            }
            return "nothing to tear";
        }
        public int PageErases { get; private set; }
        public int LockViolations { get; private set; }
        public int EccDetections { get; private set; }

        public void InjectEccError(ulong address)
        {
            var dw = address & ~7UL;
            if(!eccFaults.Add(dw)) return;
            Hook(dw);
        }

        // A machine reset drops the system bus watchpoints; the faults are in
        // the flash array and stay. Re-arm them on the first access to the
        // flash interface after a reset (the firmware sets FLASH_ACR before
        // it reads anything from the store).
        public override uint ReadDoubleWord(long offset) { Rearm(); return base.ReadDoubleWord(offset); }
        public override void WriteDoubleWord(long offset, uint value) { Rearm(); base.WriteDoubleWord(offset, value); }

        private void Rearm()
        {
            if(!needRearm) return;
            needRearm = false;
            foreach(var dw in eccFaults)
            {
                for(ulong a = dw; a < dw + 8; a++) machine.SystemBus.RemoveAllWatchpointHooks(a);
                Hook(dw);
            }
        }

        private void Hook(ulong dw)
        {
            for(ulong a = dw; a < dw + 8; a++)
                machine.SystemBus.AddWatchpointHook(a, SysbusAccessWidth.Byte | SysbusAccessWidth.Word | SysbusAccessWidth.DoubleWord,
                                                    Access.Read, OnEccRead);
        }

        private void OnEccRead(ICpuSupportingGdb cpu, ulong address, SysbusAccessWidth width, ulong value)
        {
            EccDetections++;
            var eccr = regs[U0Map.OFF_FLASH_ECCR / 4];
            if((eccr & (U0Map.FLASH_ECCR_ECCD | U0Map.FLASH_ECCR_ECCC)) == 0)
                eccr = (eccr & U0Map.FLASH_ECCR_ECCCIE) | (uint)(((address - U0Map.FLASH_BASE) >> 3) & U0Map.FLASH_ECCR_ADDR_ECC);
            regs[U0Map.OFF_FLASH_ECCR / 4] = eccr | U0Map.FLASH_ECCR_ECCD;
            nvic.SetPendingIRQ(2); // NMI
        }

        private void ClearEccFaults(ulong start, ulong length)
        {
            var gone = new List<ulong>();
            foreach(var dw in eccFaults) if(dw >= start && dw < start + length) gone.Add(dw);
            foreach(var dw in gone)
            {
                eccFaults.Remove(dw);
                for(ulong a = dw; a < dw + 8; a++) machine.SystemBus.RemoveAllWatchpointHooks(a);
            }
        }

        protected override uint OnWrite(long offset, uint old, uint value)
        {
            switch(offset)
            {
            case U0Map.OFF_FLASH_KEYR:
                if(keyStage == 0 && value == U0Map.FlashKey1) keyStage = 1;
                else if(keyStage == 1 && value == U0Map.FlashKey2) { keyStage = 2; regs[U0Map.OFF_FLASH_CR / 4] &= ~U0Map.FLASH_CR_LOCK; }
                else { keyStage = 3; LockViolations++; } // wrong sequence locks until reset
                return 0;
            case U0Map.OFF_FLASH_SR: // write 1 to clear flags
                return old & ~value;
            case U0Map.OFF_FLASH_ECCR: // ECCD, ECCC: write 1 to clear; ECCCIE: read/write
                return (old & ~(value & (U0Map.FLASH_ECCR_ECCD | U0Map.FLASH_ECCR_ECCC)) & ~U0Map.FLASH_ECCR_ECCCIE)
                       | (value & U0Map.FLASH_ECCR_ECCCIE);
            case U0Map.OFF_FLASH_OPTKEYR:
                if((regs[U0Map.OFF_FLASH_CR / 4] & U0Map.FLASH_CR_LOCK) != 0) { LockViolations++; return 0; }
                if(optKeyStage == 0 && value == U0Map.OptKey1) optKeyStage = 1;
                else if(optKeyStage == 1 && value == U0Map.OptKey2) { optKeyStage = 2; regs[U0Map.OFF_FLASH_CR / 4] &= ~U0Map.FLASH_CR_OPTLOCK; }
                else { optKeyStage = 3; LockViolations++; }
                return 0;
            case U0Map.OFF_FLASH_OPTR:
                if((regs[U0Map.OFF_FLASH_CR / 4] & U0Map.FLASH_CR_OPTLOCK) != 0) { LockViolations++; return old; }
                optrPending = value;
                return old; // reads keep showing the loaded value until OBL_LAUNCH
            case U0Map.OFF_FLASH_CR:
                if((old & U0Map.FLASH_CR_LOCK) != 0)
                {
                    if(value != old) LockViolations++;
                    return old; // locked: ignored
                }
                if((value & U0Map.FLASH_CR_LOCK) != 0) { keyStage = 0; optKeyStage = 0; return value | U0Map.FLASH_CR_OPTLOCK; }
                if((value & U0Map.FLASH_CR_PG) != 0 && (old & U0Map.FLASH_CR_PG) == 0) { programmedSinceErase = true; ProgramOps++; }
                if((value & U0Map.FLASH_CR_OPTSTRT) != 0 && (old & U0Map.FLASH_CR_OPTLOCK) == 0)
                {
                    optrProgrammed = optrPending;
                    OptionBytePrograms++;
                    regs[U0Map.OFF_FLASH_SR / 4] |= U0Map.FLASH_SR_EOP;
                    value &= ~U0Map.FLASH_CR_OPTSTRT;
                }
                if((value & U0Map.FLASH_CR_OBL_LAUNCH) != 0)
                {
                    optrLoaded = optrProgrammed;
                    Rcc?.OptionByteReset();
                    machine.RequestReset();
                    return value & ~U0Map.FLASH_CR_OBL_LAUNCH;
                }
                if((value & U0Map.FLASH_CR_STRT) != 0 && (value & U0Map.FLASH_CR_PER) != 0)
                {
                    var page = (int)((value & U0Map.FLASH_CR_PNB) >> U0Map.FLASH_CR_PNB_Pos);
                    flash.SetRange((long)page * pageSize, pageSize, 0xFF);
                    ClearEccFaults(U0Map.FLASH_BASE + (ulong)page * (ulong)pageSize, (ulong)pageSize);
                    PageErases++;
                    lastErasedPage = page;
                    programmedSinceErase = false;
                    regs[U0Map.OFF_FLASH_SR / 4] |= U0Map.FLASH_SR_EOP;
                    value &= ~U0Map.FLASH_CR_STRT;
                }
                return value;
            default:
                return value;
            }
        }

        private readonly IMachine machine;
        private readonly MappedMemory flash;
        private readonly NVIC nvic;
        private readonly int pageSize;
        private readonly HashSet<ulong> eccFaults = new HashSet<ulong>();
        private int keyStage, optKeyStage;
        private bool needRearm;
        private uint optrLoaded = 0xFFFFFEAA, optrProgrammed = 0xFFFFFEAA, optrPending = 0xFFFFFEAA;
        private int lastErasedPage = -1;
        private bool programmedSinceErase;
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
            case U0Map.OFF_EXTI_RTSR1: return rtsr;
            case U0Map.OFF_EXTI_FTSR1: return ftsr;
            case U0Map.OFF_EXTI_RPR1: return rpr;
            case U0Map.OFF_EXTI_FPR1: return fpr;
            case U0Map.OFF_EXTI_EXTICR: case U0Map.OFF_EXTI_EXTICR + 4: case U0Map.OFF_EXTI_EXTICR + 8: case U0Map.OFF_EXTI_EXTICR + 12:
                return exticr[(offset - U0Map.OFF_EXTI_EXTICR) / 4];
            case U0Map.OFF_EXTI_IMR1: return imr1;
            case U0Map.OFF_EXTI_EMR1: return emr1;
            case U0Map.OFF_EXTI_IMR2: return imr2;
            default: return 0;
            }
        }

        public void WriteDoubleWord(long offset, uint value)
        {
            switch(offset)
            {
            case U0Map.OFF_EXTI_RTSR1: rtsr = value & 0xFFFF; break;
            case U0Map.OFF_EXTI_FTSR1: ftsr = value & 0xFFFF; break;
            case U0Map.OFF_EXTI_SWIER1: rpr |= value & 0xFFFF; break;
            case U0Map.OFF_EXTI_RPR1: rpr &= ~value; break;
            case U0Map.OFF_EXTI_FPR1: fpr &= ~value; break;
            case U0Map.OFF_EXTI_EXTICR: case U0Map.OFF_EXTI_EXTICR + 4: case U0Map.OFF_EXTI_EXTICR + 8: case U0Map.OFF_EXTI_EXTICR + 12:
                exticr[(offset - U0Map.OFF_EXTI_EXTICR) / 4] = value; break;
            case U0Map.OFF_EXTI_IMR1: imr1 = value; break;
            case U0Map.OFF_EXTI_EMR1: emr1 = value; break;
            case U0Map.OFF_EXTI_IMR2: imr2 = value; break;
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
    //
    // Configuration (RM0503 21.7.1, 21.7.5): the RNG_CONFIGx/NISTC/CLKDIV/CED
    // fields and RNG_HTCR only take writes while CONDRST is 1. Words handed
    // out while the configuration is not the STM32U0 NIST one from AN4230
    // (RNG_CR 0x00F00D00, RNG_HTCR 0xAAC7; the reset value 0x00800D00 is not
    // it) are counted in UncharacterisedWords; the tests require zero.
    public class STM32U0_RNG : U0RegisterFile
    {
        public const uint NistCr = 0x00F00D00;
        public const uint NistHtcr = 0x0000AAC7;
        const uint ConfigMask = 0x03FFFFA0; // CONFIG1, CLKDIV, CONFIG2, NISTC, CONFIG3, ARDIS, CED

        public STM32U0_RNG(int seed = 1) : base(0x400) { Seed = seed; Reset(); }
        public int Seed { get => seed; set { seed = value; rng = new Random(value); } }
        public int WordsRead { get; private set; }
        public int UncharacterisedWords { get; private set; }
        protected override void OnReset()
        {
            regs[U0Map.OFF_RNG_CR / 4] = 0x00800D00;
            regs[U0Map.OFF_RNG_HTCR / 4] = 0x000072AC;
            rng = new Random(seed);
        }
        protected override uint OnWrite(long offset, uint old, uint value)
        {
            switch(offset)
            {
            case U0Map.OFF_RNG_CR:
                // The configuration fields only change while CONDRST is (or
                // is being set) 1; CONDRST itself completes at once.
                if((value & U0Map.RNG_CR_CONDRST) == 0 && (old & U0Map.RNG_CR_CONDRST) == 0)
                    value = (value & ~ConfigMask) | (old & ConfigMask);
                return value & ~U0Map.RNG_CR_CONDRST;
            case U0Map.OFF_RNG_SR: return 0;
            case U0Map.OFF_RNG_HTCR:
                // Accepted only while CONDRST = 1. The model clears CONDRST at
                // once, so it remembers the last CR write instead.
                return condrstWindow ? value : old;
            default: return value;
            }
        }
        public override void WriteDoubleWord(long offset, uint value)
        {
            base.WriteDoubleWord(offset, value);
            if(offset == U0Map.OFF_RNG_CR) condrstWindow = (value & U0Map.RNG_CR_CONDRST) != 0;
        }
        protected override uint OnRead(long offset, uint v)
        {
            bool on = (regs[U0Map.OFF_RNG_CR / 4] & U0Map.RNG_CR_RNGEN) != 0;
            if(offset == U0Map.OFF_RNG_SR) return on ? U0Map.RNG_SR_DRDY : 0u;
            if(offset == U0Map.OFF_RNG_DR)
            {
                if(!on) return 0;
                WordsRead++;
                if((regs[U0Map.OFF_RNG_CR / 4] & ConfigMask) != (NistCr & ConfigMask) || regs[U0Map.OFF_RNG_HTCR / 4] != NistHtcr)
                {
                    if(UncharacterisedWords++ == 0)
                        this.Log(LogLevel.Warning, "random words drawn with RNG_CR 0x{0:X8}, RNG_HTCR 0x{1:X}: not the AN4230 configuration",
                                 regs[U0Map.OFF_RNG_CR / 4], regs[U0Map.OFF_RNG_HTCR / 4]);
                }
                uint w;
                do { w = (uint)rng.Next(0x10000) << 16 | (uint)rng.Next(0x10000); } while(w == 0);
                return w;
            }
            return v;
        }
        private int seed;
        private Random rng;
        private bool condrstWindow;
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
            case U0Map.OFF_ADC_ISR: return old & ~value; // write 1 to clear
            case U0Map.OFF_ADC_CR:
                value &= ~U0Map.ADC_CR_ADCAL; // calibration completes at once
                if((value & U0Map.ADC_CR_ADDIS) != 0) value &= ~(U0Map.ADC_CR_ADEN | U0Map.ADC_CR_ADDIS);
                else if((value & U0Map.ADC_CR_ADEN) != 0 && (old & U0Map.ADC_CR_ADEN) == 0) regs[U0Map.OFF_ADC_ISR / 4] |= U0Map.ADC_ISR_ADRDY;
                if((value & U0Map.ADC_CR_ADSTART) != 0 && (value & U0Map.ADC_CR_ADEN) != 0)
                {
                    regs[U0Map.OFF_ADC_DR / 4] = Convert();
                    regs[U0Map.OFF_ADC_ISR / 4] |= U0Map.ADC_ISR_EOC | U0Map.ADC_ISR_EOS;
                    value &= ~U0Map.ADC_CR_ADSTART;
                }
                return value;
            case U0Map.OFF_ADC_CHSELR: regs[U0Map.OFF_ADC_ISR / 4] |= U0Map.ADC_ISR_CCRDY; return value;
            default: return value;
            }
        }

        private uint Convert()
        {
            Conversions++;
            uint sel = regs[U0Map.OFF_ADC_CHSELR / 4] & 0x7FFFF;
            int ch = 0;
            while(ch < 19 && (sel & (1u << ch)) == 0) ch++;
            decimal mv = ch == 12 ? VrefintMillivolts : 0;
            var raw = (int)Math.Round(mv / VddaMillivolts * 4095);
            return (uint)Math.Max(0, Math.Min(4095, raw));
        }
    }
}
