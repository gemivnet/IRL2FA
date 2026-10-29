// Power instrumentation for the emulated IRL2FA board. Not a model of any
// chip: an observer that records, between Start() and Stop(), everything the
// battery budget (hardware/power/power.py) needs from the real firmware:
//
//   MCU      Stop 2 entries and time (WFI with SCB->SCR.SLEEPDEEP set), the
//            instructions executed while awake (run time = instructions /
//            MIPS; the rest of the awake time the core sits in WFI, i.e.
//            Sleep mode), and a 10 ms histogram of instructions for the
//            timeline.
//   e-paper  supply switch on/off, BUSY periods by kind (full, partial,
//            reset...), SPI bytes (from the SSD1680 model).
//   RTC      I2C reads and writes with their byte counts (RV3032 model).
//   IR       receiver supply (IR_RX_PWR, GPIO input 0) on/off and every
//            byte USART1 sends; with TX inverted the LED is lit for the
//            start bit and each 0 data bit.
//
// Save(path) writes the timeline and totals as JSON for
// hardware/power/power.py --measured. Datasheet currents are applied there,
// not here: this file only counts.
// SPDX-License-Identifier: AGPL-3.0-or-later
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using Antmicro.Renode.Core;
using Antmicro.Renode.Peripherals.Bus;
using Antmicro.Renode.Peripherals.CPU;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Peripherals.UART;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    public class IRL2FA_PowerMonitor : IDoubleWordPeripheral, IKnownSize, IGPIOReceiver
    {
        public IRL2FA_PowerMonitor(IMachine machine, TranslationCPU cpu, IUART irUart, SSD1680 epd, RV3032 rtc,
                                   int mips = 16, int irBaud = 4800)
        {
            this.machine = machine;
            this.cpu = cpu;
            this.mips = mips;
            this.irBaud = irBaud;
            cpu.AddHookAtWfiStateChange(OnWfi);
            irUart.CharReceived += OnIrByte;
            epd.Activity += OnEpd;
            rtc.Activity += OnRtc;
            this.epd = epd;
            this.rtc = rtc;
            sampler = new LimitTimer(machine.ClockSource, 1000, this, "power_sample", SampleMs, Direction.Ascending, false, WorkMode.Periodic, true);
            sampler.LimitReached += Sample;
        }

        public const int SampleMs = 10;
        public bool Running => running;

        public void Reset() { }

        // Renode only names peripherals on a bus, so the monitor sits on a
        // 4-byte window in a region the STM32U0 leaves unused (it reads 0).
        public long Size => 4;
        public uint ReadDoubleWord(long offset) { return 0; }
        public void WriteDoubleWord(long offset, uint value) { }

        // GPIO 0: IR receiver supply (PA11, high = on).
        public void OnGPIO(int number, bool value)
        {
            if(number != 0 || value == irOn) return;
            irOn = value;
            if(!running) return;
            var t = Now();
            if(value) irOnAt = t;
            else irOnMs += t - irOnAt;
            Event(t, "ir", value ? "on" : "off");
        }

        public void Start(string session)
        {
            this.session = session;
            events.Clear();
            samples.Clear();
            t0 = Now();
            startUnixMs = rtc.GetUnixTimeMs();
            instr0 = lastSampleInstr = cpu.ExecutedInstructions;
            stop2Ms = epdPoweredMs = irOnMs = 0;
            busyMs.Clear(); busyCount.Clear();
            stop2Entries = wfiEntries = 0;
            i2cReads = i2cWrites = i2cBytes = 0;
            irTxBytes = irLedBits = 0;
            spi0 = epd.SpiBytes;
            inStop2 = cpu.IsHalted && DeepSleep();
            stop2At = t0;
            epdOn = epd.Powered; epdOnAt = t0;
            busyKind = null;
            irOnAt = t0;
            running = true;
            Event(t0, "start", session);
            Event(t0, "mcu", inStop2 ? "stop2" : "run");
            sampler.Value = 0;
            sampler.Enabled = true;
        }

        public void Mark(string label)
        {
            if(running) Event(Now(), "mark", label);
        }

        public void Stop()
        {
            if(!running) return;
            var t = Now();
            if(inStop2) stop2Ms += t - stop2At;
            if(epdOn) epdPoweredMs += t - epdOnAt;
            if(busyKind != null) AddBusy(busyKind, t - busyAt);
            if(irOn) irOnMs += t - irOnAt;
            t1 = t;
            instr1 = cpu.ExecutedInstructions;
            spi1 = epd.SpiBytes;
            sampler.Enabled = false;
            Event(t, "stop", session);
            running = false;
        }

        public void Save(string path)
        {
            if(running) Stop();
            var inv = CultureInfo.InvariantCulture;
            var sb = new StringBuilder();
            sb.Append("{\n");
            sb.AppendFormat(inv, "  \"session\": \"{0}\",\n", session);
            sb.AppendFormat(inv, "  \"mips\": {0},\n  \"ir_baud\": {1},\n  \"sample_ms\": {2},\n", mips, irBaud, SampleMs);
            sb.AppendFormat(inv, "  \"duration_ms\": {0:0.###},\n", t1 - t0);
            sb.AppendFormat(inv, "  \"start_unix_ms\": {0:0.###},\n", startUnixMs);
            sb.AppendFormat(inv, "  \"instructions\": {0},\n", instr1 - instr0);
            sb.AppendFormat(inv, "  \"stop2_ms\": {0:0.###},\n  \"stop2_entries\": {1},\n  \"wfi_entries\": {2},\n", stop2Ms, stop2Entries, wfiEntries);
            sb.AppendFormat(inv, "  \"epd_powered_ms\": {0:0.###},\n  \"spi_bytes\": {1},\n", epdPoweredMs, spi1 - spi0);
            sb.Append("  \"epd_busy_ms\": {");
            sb.Append(string.Join(", ", Kinds(k => string.Format(inv, "\"{0}\": {1:0.###}", k, busyMs[k]))));
            sb.Append("},\n  \"epd_busy_count\": {");
            sb.Append(string.Join(", ", Kinds(k => string.Format(inv, "\"{0}\": {1}", k, busyCount[k]))));
            sb.Append("},\n");
            sb.AppendFormat(inv, "  \"i2c_reads\": {0},\n  \"i2c_writes\": {1},\n  \"i2c_bytes\": {2},\n", i2cReads, i2cWrites, i2cBytes);
            sb.AppendFormat(inv, "  \"ir_on_ms\": {0:0.###},\n  \"ir_tx_bytes\": {1},\n  \"ir_led_bits\": {2},\n", irOnMs, irTxBytes, irLedBits);
            sb.Append("  \"instructions_per_sample\": [");
            sb.Append(string.Join(",", samples));
            sb.Append("],\n  \"events\": [\n");
            for(int i = 0; i < events.Count; i++)
                sb.Append("    ").Append(events[i]).Append(i + 1 < events.Count ? ",\n" : "\n");
            sb.Append("  ]\n}\n");
            File.WriteAllText(path, sb.ToString());
        }

        // ---- observers ----

        private void OnWfi(bool entering)
        {
            if(!running) return;
            var t = Now();
            if(entering)
            {
                wfiEntries++;
                if(DeepSleep() && !inStop2)
                {
                    inStop2 = true;
                    stop2At = t;
                    stop2Entries++;
                    Event(t, "mcu", "stop2");
                }
            }
            else if(inStop2)
            {
                inStop2 = false;
                stop2Ms += t - stop2At;
                Event(t, "mcu", "run");
            }
        }

        private void OnIrByte(byte b)
        {
            if(!running) return;
            irTxBytes++;
            int zeros = 8;
            for(int v = b; v != 0; v >>= 1) zeros -= v & 1;
            irLedBits += 1 + zeros; // start bit + 0 data bits light the LED
            Event(Now(), "ir_tx", string.Format("0x{0:X2}", b));
        }

        private void OnEpd(string what)
        {
            if(!running) { epdOn = epd.Powered; return; }
            var t = Now();
            if(what == "on") { epdOn = true; epdOnAt = t; }
            else if(what == "off") { if(epdOn) epdPoweredMs += t - epdOnAt; epdOn = false; }
            else if(what.StartsWith("busy:")) { busyKind = what.Substring(5); busyAt = t; }
            else if(what == "idle" && busyKind != null) { AddBusy(busyKind, t - busyAt); busyKind = null; }
            Event(t, "epd", what);
        }

        private void OnRtc(string what, int bytes)
        {
            if(!running) return;
            if(what == "read") i2cReads++; else i2cWrites++;
            i2cBytes += bytes;
            Event(Now(), "i2c", what + ":" + bytes);
        }

        private void Sample()
        {
            if(!running) return;
            var now = cpu.ExecutedInstructions;
            samples.Add((long)(now - lastSampleInstr));
            lastSampleInstr = now;
        }

        // ---- helpers ----

        private bool DeepSleep()
        {
            // SCB->SCR (0xE000ED10) bit 2 SLEEPDEEP: the firmware sets it only
            // around the WFI in hal_sleep() (Stop 2, PWR_CR1.LPMS = 2).
            return (machine.SystemBus.ReadDoubleWord(0xE000ED10) & 4u) != 0;
        }

        private void AddBusy(string kind, double ms)
        {
            if(!busyMs.ContainsKey(kind)) { busyMs[kind] = 0; busyCount[kind] = 0; }
            busyMs[kind] += ms;
            busyCount[kind]++;
        }

        private IEnumerable<string> Kinds(Func<string, string> f)
        {
            var keys = new List<string>(busyMs.Keys);
            keys.Sort(StringComparer.Ordinal);
            foreach(var k in keys) yield return f(k);
        }

        private void Event(double t, string what, string detail)
        {
            events.Add(string.Format(CultureInfo.InvariantCulture, "[{0:0.###}, \"{1}\", \"{2}\"]", t - t0, what, detail));
        }

        private double Now() { return machine.LocalTimeSource.ElapsedVirtualTime.TotalMilliseconds; }

        private readonly IMachine machine;
        private readonly TranslationCPU cpu;
        private readonly SSD1680 epd;
        private readonly RV3032 rtc;
        private readonly LimitTimer sampler;
        private readonly int mips, irBaud;
        private readonly List<string> events = new List<string>();
        private readonly List<long> samples = new List<long>();
        private readonly Dictionary<string, double> busyMs = new Dictionary<string, double>();
        private readonly Dictionary<string, int> busyCount = new Dictionary<string, int>();
        private string session = "", busyKind;
        private bool running, inStop2, epdOn, irOn;
        private double startUnixMs;
        private double t0, t1, stop2At, stop2Ms, epdOnAt, epdPoweredMs, busyAt, irOnAt, irOnMs;
        private ulong instr0, instr1, lastSampleInstr;
        private long spi0, spi1;
        private int stop2Entries, wfiEntries, i2cReads, i2cWrites, i2cBytes, irTxBytes, irLedBits;
    }
}
