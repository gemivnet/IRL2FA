// Renode model of a Solomon SSD1680 e-paper controller driving a 122 x 250
// panel (Good Display GDEY0213B74) as mounted on the IRL2FA board.
//
// Implemented from the SSD1680 datasheet (Rev 0.14): 4-wire SPI with D/C#
// and CS#, the BW (0x24) and RED (0x26) RAMs with the address counters and
// windows of commands 0x11/0x44/0x45/0x4E/0x4F, SW reset 0x12, deep sleep
// 0x10, display update control 0x22 and master activation 0x20 with BUSY
// held high for a configurable time. The waveforms are not simulated: when
// an update finishes, the panel "shows" the BW RAM. A display mode 2
// (partial) update also checks that the RED RAM holds the image the panel
// showed before, which is what the differential waveform relies on.
//
// GPIO inputs: 0 = D/C#, 1 = CS#, 2 = RES#, 3 = supply switch (low = on).
// GPIO output BUSY (high = busy).
//
// Panel geometry (shared assumption with firmware/port/stm32u0/epd_ssd1680.c,
// documented there): landscape pixel (lx, ly) of the 250 x 122 view is RAM
// x = ly, y = 249 - lx.
// SPDX-License-Identifier: AGPL-3.0-or-later
using System;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using Antmicro.Renode.Core;
using Antmicro.Renode.Logging;
using Antmicro.Renode.Peripherals.SPI;
using Antmicro.Renode.Peripherals.Timers;
using Antmicro.Renode.Time;

namespace Antmicro.Renode.Peripherals.Miscellaneous
{
    public class SSD1680 : ISPIPeripheral, IGPIOReceiver
    {
        public const int Width = 250, Height = 122;
        const int RamX = 22, RamY = 296; // 176 x 296 controller RAM

        public SSD1680(IMachine machine, int fullRefreshMs = 3000, int partialRefreshMs = 420)
        {
            this.machine = machine;
            BUSY = new GPIO();
            FullRefreshMs = fullRefreshMs;
            PartialRefreshMs = partialRefreshMs;
            busyTimer = new LimitTimer(machine.ClockSource, 1000, this, "busy", 1, Direction.Ascending, false, WorkMode.OneShot, true);
            busyTimer.LimitReached += BusyDone;
            Reset();
        }

        public GPIO BUSY { get; }
        public int FullRefreshMs { get; set; }
        public int PartialRefreshMs { get; set; }
        public int FullUpdates { get; private set; }
        public int PartialUpdates { get; private set; }
        public int PartialOldImageMismatches { get; private set; }
        public int BackPowerViolations { get; private set; }
        public int CommandsWhileBusy { get; private set; }
        // Timing of the power-up sequence (SSD1680 section 9 and Good
        // Display's GDEY0213B74 reference flow): VCI must have been on for
        // 10 ms before the first command, and a hardware reset holds RES#
        // low for 10 ms. Each shortfall is counted; the tests require zero.
        public int PowerUpTimingViolations { get; private set; }
        public int PowerCycles { get; private set; }
        public int BytesIgnored { get; private set; }
        public bool Powered => powered;
        public bool DeepSleep => deepSleep;
        public bool IsBusy => busy;
        public string LastUpdate { get; private set; } = "none";
        // Bytes clocked in over SPI while CS# was low (commands, parameters,
        // RAM data), whatever the panel did with them: the bus traffic.
        public long SpiBytes { get; private set; }
        // For the power instrumentation (IRL2FA_PowerMonitor): "on", "off",
        // "busy:<kind>" with kind reset|swreset|nodisplay|full|partial, "idle".
        public event Action<string> Activity;

        public void Reset()
        {
            pins = new bool[] { false, true, true, true };
            powered = false;
            PowerOnReset();
            Array.Clear(shown, 0, shown.Length);
            BUSY.Unset();
        }

        public void OnGPIO(int number, bool value)
        {
            if(number < 0 || number > 3) return;
            var old = pins[number];
            pins[number] = value;
            if(number == 3)
            {
                bool on = !value;
                if(on && !powered) { powered = true; PowerCycles++; PowerOnReset(); poweredAtMs = NowMs(); commandSincePower = false; Activity?.Invoke("on"); }
                else if(!on && powered)
                {
                    if(busy) Activity?.Invoke("idle");
                    powered = false; busyTimer.Enabled = false; busy = false; BUSY.Unset();
                    Activity?.Invoke("off");
                }
            }
            if(!powered && number < 3 && value)
            {
                BackPowerViolations++;
                this.Log(LogLevel.Warning, "line {0} driven high while the panel is unpowered", number);
            }
            if(number == 2 && powered)
            {
                if(!value) { inReset = true; resetAtMs = NowMs(); }
                else if(!old)
                {
                    inReset = false;
                    if(NowMs() - resetAtMs < 10.0 - 1e-6)
                    {
                        PowerUpTimingViolations++;
                        this.Log(LogLevel.Warning, "RES# held low for only {0:0.###} ms (10 ms required)", NowMs() - resetAtMs);
                    }
                    RegisterReset(); StartBusy(1, "reset");
                }
            }
            if(number == 1 && value) { haveCommand = false; } // CS# rising ends the transfer
        }

        public byte Transmit(byte data)
        {
            if(!pins[1]) SpiBytes++;
            if(!powered || inReset || pins[1]) { BytesIgnored++; return 0; }
            if(deepSleep) { BytesIgnored++; return 0; }
            if(!pins[0] && !commandSincePower)
            {
                commandSincePower = true;
                if(NowMs() - poweredAtMs < 10.0 - 1e-6)
                {
                    PowerUpTimingViolations++;
                    this.Log(LogLevel.Warning, "first command {0:0.###} ms after VCI on (10 ms required)", NowMs() - poweredAtMs);
                }
            }
            if(!pins[0]) Command(data);
            else Data(data);
            return 0;
        }

        public void FinishTransmission() { }

        // ---- queries for tests ----
        public bool Pixel(int x, int y) { return shown[y * Width + x]; }

        public int BlackPixels(int x, int y, int w, int h)
        {
            int n = 0;
            for(int j = Math.Max(0, y); j < Math.Min(Height, y + h); j++)
                for(int i = Math.Max(0, x); i < Math.Min(Width, x + w); i++)
                    if(shown[j * Width + i]) n++;
            return n;
        }

        public string RegionHash(int x, int y, int w, int h)
        {
            var sb = new StringBuilder();
            for(int j = y; j < y + h; j++)
                for(int i = x; i < x + w; i++)
                    sb.Append(shown[j * Width + i] ? '1' : '0');
            using(var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(Encoding.ASCII.GetBytes(sb.ToString()))).Replace("-", "").ToLowerInvariant().Substring(0, 16);
        }

        public string DisplayHash() { return RegionHash(0, 0, Width, Height); }

        public void DumpPbm(string path)
        {
            using(var f = File.Create(path))
            {
                var hdr = Encoding.ASCII.GetBytes($"P4\n{Width} {Height}\n");
                f.Write(hdr, 0, hdr.Length);
                f.Write(Packed(), 0, (Width + 7) / 8 * Height);
            }
        }

        public void DumpPng(string path, int scale = 2)
        {
            int w = Width * scale, h = Height * scale;
            var raw = new MemoryStream();
            for(int y = 0; y < h; y++)
            {
                raw.WriteByte(0); // filter: none
                for(int x = 0; x < w; x++) raw.WriteByte(shown[(y / scale) * Width + x / scale] ? (byte)0x1C : (byte)0xE9);
            }
            byte[] idat;
            using(var z = new MemoryStream())
            {
                using(var zs = new ZLibStream(z, CompressionLevel.Optimal, true)) raw.WriteTo(zs);
                idat = z.ToArray();
            }
            using(var f = File.Create(path))
            {
                f.Write(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }, 0, 8);
                var ihdr = new byte[13];
                Be(ihdr, 0, (uint)w); Be(ihdr, 4, (uint)h);
                ihdr[8] = 8; ihdr[9] = 0; // 8-bit greyscale
                Chunk(f, "IHDR", ihdr);
                Chunk(f, "IDAT", idat);
                Chunk(f, "IEND", new byte[0]);
            }
        }

        // ---- controller ----
        private void PowerOnReset()
        {
            inReset = false;
            deepSleep = false;
            busy = false;
            haveCommand = false;
            var r = new Random(1234); // RAM content is undefined after power-up
            r.NextBytes(bw); r.NextBytes(red);
            RegisterReset();
        }

        private void RegisterReset()
        {
            entryMode = 0x03;
            xStart = 0; xEnd = RamX - 1; yStart = 0; yEnd = RamY - 1;
            xCount = 0; yCount = 0;
            updateOption = 0xFF;
            deepSleep = false;
        }

        private void Command(byte c)
        {
            if(busy && c != 0x10) CommandsWhileBusy++;
            cmd = c;
            haveCommand = true;
            param = 0;
            switch(c)
            {
            case 0x12: RegisterReset(); StartBusy(2, "swreset"); break;
            case 0x20: Activate(); break;
            }
        }

        private void Data(byte d)
        {
            if(!haveCommand) return;
            int p = param++;
            switch(cmd)
            {
            case 0x10: if(p == 0 && (d & 3) != 0) deepSleep = true; break;
            case 0x11: if(p == 0) entryMode = d & 7; break;
            case 0x44: if(p == 0) xStart = d & 0x3F; else if(p == 1) xEnd = d & 0x3F; break;
            case 0x45:
                if(p == 0) yStart = (yStart & 0x100) | d;
                else if(p == 1) yStart = (yStart & 0xFF) | ((d & 1) << 8);
                else if(p == 2) yEnd = (yEnd & 0x100) | d;
                else if(p == 3) yEnd = (yEnd & 0xFF) | ((d & 1) << 8);
                break;
            case 0x4E: if(p == 0) xCount = d & 0x3F; break;
            case 0x4F:
                if(p == 0) yCount = (yCount & 0x100) | d;
                else if(p == 1) yCount = (yCount & 0xFF) | ((d & 1) << 8);
                break;
            case 0x22: if(p == 0) updateOption = d; break;
            case 0x24: WriteRam(bw, d); break;
            case 0x26: WriteRam(red, d); break;
            }
        }

        // Address counter per Data Entry Mode (0x11): ID[1:0] = Y/X increment,
        // AM = 1 updates Y first. Counters wrap inside the window.
        private void WriteRam(byte[] ram, byte d)
        {
            if(xCount < RamX && yCount < RamY) ram[yCount * RamX + xCount] = d;
            bool xInc = (entryMode & 1) != 0, yInc = (entryMode & 2) != 0, yFirst = (entryMode & 4) != 0;
            if(!yFirst)
            {
                if(StepX(xInc)) StepY(yInc);
            }
            else
            {
                if(StepY(yInc)) StepX(xInc);
            }
        }

        private bool StepX(bool inc)
        {
            if(inc) { if(xCount >= xEnd) { xCount = xStart; return true; } xCount++; }
            else { if(xCount <= xStart) { xCount = xEnd; return true; } xCount--; }
            return false;
        }

        private bool StepY(bool inc)
        {
            if(inc) { if(yCount >= yEnd) { yCount = yStart; return true; } yCount++; }
            else { if(yCount <= yStart) { yCount = yEnd; return true; } yCount--; }
            return false;
        }

        private void Activate()
        {
            // 0x22 option bit 2 = display; bit 3 selects display mode 2.
            if((updateOption & 0x04) == 0) { StartBusy(5, "nodisplay"); pendingDisplay = 0; return; }
            bool mode2 = (updateOption & 0x08) != 0;
            pendingDisplay = mode2 ? 2 : 1;
            StartBusy(mode2 ? PartialRefreshMs : FullRefreshMs, mode2 ? "partial" : "full");
        }

        private void StartBusy(int ms, string kind)
        {
            if(busy) Activity?.Invoke("idle");
            Activity?.Invoke("busy:" + kind);
            busy = true;
            BUSY.Set();
            busyTimer.Enabled = false;
            busyTimer.Limit = (ulong)Math.Max(1, ms);
            busyTimer.Value = 0;
            busyTimer.Enabled = true;
        }

        private void BusyDone()
        {
            busy = false;
            BUSY.Unset();
            Activity?.Invoke("idle");
            if(pendingDisplay == 0) return;
            var next = new bool[Width * Height];
            bool mismatch = false;
            for(int ly = 0; ly < Height; ly++)
                for(int lx = 0; lx < Width; lx++)
                {
                    int x = ly, y = Width - 1 - lx;
                    int idx = y * RamX + x / 8, bit = 7 - x % 8;
                    next[ly * Width + lx] = ((bw[idx] >> bit) & 1) == 0;
                    bool old = ((red[idx] >> bit) & 1) == 0;
                    if(pendingDisplay == 2 && old != shown[ly * Width + lx]) mismatch = true;
                }
            if(pendingDisplay == 2) { PartialUpdates++; if(mismatch) PartialOldImageMismatches++; LastUpdate = "partial"; }
            else { FullUpdates++; LastUpdate = "full"; }
            Array.Copy(next, shown, shown.Length);
            pendingDisplay = 0;
        }

        private byte[] Packed()
        {
            int stride = (Width + 7) / 8;
            var b = new byte[stride * Height];
            for(int y = 0; y < Height; y++)
                for(int x = 0; x < Width; x++)
                    if(shown[y * Width + x]) b[y * stride + x / 8] |= (byte)(0x80 >> (x % 8));
            return b;
        }

        private static void Be(byte[] b, int o, uint v) { b[o] = (byte)(v >> 24); b[o + 1] = (byte)(v >> 16); b[o + 2] = (byte)(v >> 8); b[o + 3] = (byte)v; }

        private static void Chunk(Stream s, string type, byte[] data)
        {
            var len = new byte[4]; Be(len, 0, (uint)data.Length);
            s.Write(len, 0, 4);
            var td = new byte[4 + data.Length];
            Encoding.ASCII.GetBytes(type).CopyTo(td, 0);
            data.CopyTo(td, 4);
            s.Write(td, 0, td.Length);
            var crc = new byte[4]; Be(crc, 0, Crc32(td));
            s.Write(crc, 0, 4);
        }

        private static uint Crc32(byte[] d)
        {
            uint c = 0xFFFFFFFF;
            foreach(var x in d) { c ^= x; for(int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint)-(int)(c & 1)); }
            return ~c;
        }

        private readonly LimitTimer busyTimer;
        private readonly byte[] bw = new byte[RamX * RamY];
        private readonly byte[] red = new byte[RamX * RamY];
        private readonly bool[] shown = new bool[Width * Height];
        private bool[] pins;
        private bool powered, inReset, deepSleep, busy, haveCommand;
        private int cmd, param, entryMode, xStart, xEnd, yStart, yEnd, xCount, yCount, updateOption, pendingDisplay;
    
        private double NowMs() { return machine.LocalTimeSource.ElapsedVirtualTime.TotalMilliseconds; }
        private readonly IMachine machine;
        private double poweredAtMs, resetAtMs;
        private bool commandSincePower;
    }
}
