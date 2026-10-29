// Renode model of the Micro Crystal RV-3032-C7 RTC (I2C address 0x51), from
// the RV-3032-C7 Application Manual Rev 1.3: auto-incrementing register
// pointer, BCD clock/calendar 01h-07h running on emulated virtual time, the
// hundredths register, Status flags (write 0 to clear, PORF set at power-on),
// user RAM 40h-4Fh, the configuration RAM mirror C0h-CAh with the EECMD 11h
// "update" command into a model EEPROM, and the periodic countdown timer
// (TD, TE, TIE, TF) pulsing the open-drain INT output low.
// Not modelled: alarms, periodic time update, temperature, timestamps, EVI,
// backup switchover, password protection, CLKOUT waveform.
// SPDX-License-Identifier: AGPL-3.0-or-later
using System;
using System.Collections.Generic;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.I2C;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    public class RV3032 : II2CPeripheral
    {
        public RV3032(IMachine machine, long initialUnixTime = 946684800)
        {
            this.machine = machine;
            INT = new GPIO();
            countdown = new LimitTimer(machine.ClockSource, 4096, this, "countdown", 1, Direction.Ascending, false, WorkMode.Periodic, true);
            countdown.LimitReached += TimerFired;
            pulse = new LimitTimer(machine.ClockSource, 1000, this, "int_pulse", 8, Direction.Ascending, false, WorkMode.OneShot, true);
            pulse.LimitReached += () => { INT.Set(); };
            this.initialUnixTime = initialUnixTime;
            Reset();
        }

        public GPIO INT { get; }
        public int EepromUpdates { get; private set; }
        public int TimerInterrupts { get; private set; }
        public bool ClkoutEnabled => (regs[0xC0] & 0x40) == 0;
        // Bus activity for the power instrumentation: ("write"|"read", bytes
        // on the bus including the address byte).
        public event Action<string, int> Activity;

        public void Reset()
        {
            Array.Clear(regs, 0, regs.Length);
            Array.Clear(eeprom, 0, eeprom.Length);
            regs[0x0D] = 0x02; // PORF
            regs[0x3D] = 0xC0;
            pointer = 0;
            expectAddress = true;
            countdown.Enabled = false;
            pulse.Enabled = false;
            SetUnixTime(initialUnixTime);
            INT.Set(); // open drain, pulled up
        }

        // ---- test controls ----
        public void SetUnixTime(long t) { baseUnix = t; baseMs = NowMs(); }
        public long GetUnixTime() { return baseUnix + (long)Math.Floor((NowMs() - baseMs) / 1000.0); }
        public double GetUnixTimeMs() { return baseUnix * 1000.0 + (NowMs() - baseMs); }
        public void ClearPowerOnFlag() { regs[0x0D] &= 0xFC; }
        public void PokeRegister(int address, int value) { regs[address & 0xFF] = (byte)value; }
        public int PeekRegister(int address) { return Read((byte)address); }

        // ---- I2C ----
        public void Write(byte[] data)
        {
            Activity?.Invoke("write", data.Length + 1);
            foreach(var b in data)
            {
                if(expectAddress) { pointer = b; expectAddress = false; continue; }
                WriteRegister(pointer, b);
                pointer = (byte)(pointer + 1);
            }
        }

        public byte[] Read(int count = 1)
        {
            Activity?.Invoke("read", count + 1);
            var r = new byte[count];
            for(int i = 0; i < count; i++) { r[i] = Read(pointer); pointer = (byte)(pointer + 1); }
            expectAddress = true;
            return r;
        }

        public void FinishTransmission() { expectAddress = true; }

        private byte Read(byte a)
        {
            if(a <= 0x07)
            {
                var c = Civil(GetUnixTime());
                switch(a)
                {
                case 0: return Bcd((int)(((NowMs() - baseMs) % 1000 + 1000) % 1000 / 10));
                case 1: return Bcd(c.Second);
                case 2: return Bcd(c.Minute);
                case 3: return Bcd(c.Hour);
                case 4: return (byte)((int)c.DayOfWeek);
                case 5: return Bcd(c.Day);
                case 6: return Bcd(c.Month);
                case 7: return Bcd(c.Year - 2000);
                }
            }
            if(a == 0x39 || a == 0x3A || a == 0x3B || a == 0x3C || a == 0x3F) return 0; // write only
            if((a >= 0x2E && a <= 0x38) || (a >= 0x50 && a <= 0xBF) || a >= 0xCB) return 0;
            return regs[a];
        }

        private void WriteRegister(byte a, byte v)
        {
            if(a >= 0x01 && a <= 0x07)
            {
                var c = Civil(GetUnixTime());
                int sec = c.Second, min = c.Minute, hour = c.Hour, day = c.Day, mon = c.Month, year = c.Year;
                switch(a)
                {
                case 1: sec = FromBcd(v & 0x7F); break;
                case 2: min = FromBcd(v & 0x7F); break;
                case 3: hour = FromBcd(v & 0x3F); break;
                case 4: return; // weekday: stored by the chip, not used for the date
                case 5: day = FromBcd(v & 0x3F); break;
                case 6: mon = FromBcd(v & 0x1F); break;
                case 7: year = 2000 + FromBcd(v); break;
                }
                mon = Math.Max(1, Math.Min(12, mon));
                day = Math.Max(1, Math.Min(DateTime.DaysInMonth(year, mon), day));
                var t = new DateTime(year, mon, day, Math.Min(hour, 23), Math.Min(min, 59), Math.Min(sec, 59), DateTimeKind.Utc);
                baseUnix = new DateTimeOffset(t).ToUnixTimeSeconds();
                if(a == 1) baseMs = NowMs(); // writing Seconds restarts the prescaler
                else baseMs = NowMs() - ((NowMs() - baseMs) % 1000);
                return;
            }
            switch(a)
            {
            case 0x0D: regs[a] &= v; regs[a] &= 0x3F; return; // flags: write 0 clears; THF/TLF always reset
            case 0x0E: regs[a] = (byte)((regs[a] & ~0x08) | (v & regs[a] & 0x08)); return; // EEF clear-only
            case 0x0F: return;
            case 0x10:
            {
                bool teWas = (regs[a] & 0x08) != 0;
                regs[a] = (byte)(v & 0x3F);
                bool te = (v & 0x08) != 0;
                if(te && !teWas) StartCountdown();
                else if(!te) countdown.Enabled = false;
                return;
            }
            case 0x3F:
                if(v == 0x11) { Array.Copy(regs, 0xC0, eeprom, 0, 11); EepromUpdates++; }
                else if(v == 0x12) Array.Copy(eeprom, 0, regs, 0xC0, 11);
                else if(v == 0x21) eeprom[(regs[0x3D] - 0xC0) & 0x3F] = regs[0x3E];
                else if(v == 0x22) regs[0x3E] = eeprom[(regs[0x3D] - 0xC0) & 0x3F];
                return;
            }
            if((a >= 0x2E && a <= 0x38) || (a >= 0x50 && a <= 0xBF) || a >= 0xCB || a == 0x00) return;
            regs[a] = v;
        }

        private void StartCountdown()
        {
            int value = regs[0x0B] | ((regs[0x0C] & 0x0F) << 8);
            if(value == 0) return;
            // TD: 00 = 4096 Hz, 01 = 64 Hz, 10 = 1 Hz, 11 = 1/60 Hz
            double periodMs = new[] { 1000.0 / 4096, 1000.0 / 64, 1000.0, 60000.0 }[regs[0x10] & 3] * value;
            countdown.Enabled = false;
            countdown.Frequency = 1000;
            countdown.Limit = (ulong)Math.Max(1, Math.Round(periodMs));
            countdown.Value = 0;
            countdown.Enabled = true;
        }

        private void TimerFired()
        {
            regs[0x0D] |= 0x10; // TF
            TimerInterrupts++;
            if((regs[0x11] & 0x10) != 0) // TIE: INT pulses low for tRTN1
            {
                INT.Unset();
                pulse.Value = 0;
                pulse.Enabled = true;
            }
        }

        private double NowMs() { return machine.LocalTimeSource.ElapsedVirtualTime.TotalMilliseconds; }
        private static DateTime Civil(long t) { return DateTimeOffset.FromUnixTimeSeconds(t).UtcDateTime; }
        private static byte Bcd(int v) { return (byte)(((v / 10) << 4) | (v % 10)); }
        private static int FromBcd(int v) { return (v >> 4) * 10 + (v & 0xF); }

        private readonly IMachine machine;
        private readonly LimitTimer countdown, pulse;
        private readonly byte[] regs = new byte[256];
        private readonly byte[] eeprom = new byte[11];
        private readonly long initialUnixTime;
        private long baseUnix;
        private double baseMs;
        private byte pointer;
        private bool expectAddress;
    }
}
