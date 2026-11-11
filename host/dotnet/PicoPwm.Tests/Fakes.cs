// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using System.Buffers.Binary;
using PicoPwm.Host;

namespace PicoPwm.Tests;

/// <summary>In-memory HID reports and device doubles shared by host tests.</summary>
internal static class Fakes
{
    internal static readonly byte[] Identity = [1, 3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 2, 24, 3, 0x1F];
    internal static readonly byte[] Configuration =
        [1, 0, 1, 2, 0, 0, 1, 0x20, 0, 1, 2, 0, 0, 1, 0x20, 1, 1, 0x80, 0xD1, 0xF0, 0x08];
    internal static readonly byte[] Bank = [1, .. Enumerable.Range(0, 8).SelectMany(_ => new byte[] { 0xE8, 0x03, 0, 0, 50 })];

    internal sealed class FakeDevice(string mode = "ok") : IFeatureDevice
    {
        public byte[] Result = [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
        public List<byte[]> Sent = [];
        public byte[] IdentityReport = Identity;
        public bool Closed;
        public bool InvalidPins;
        public bool Unplugged;
        public IOException? CloseError;
        public Action<byte>? BeforeGet;
        public Action<byte[]>? BeforeSet;
        public uint[] FreqHz = Enumerable.Repeat(1000u, Protocol.ChannelCount).ToArray();
        public byte[] Duty = Enumerable.Repeat((byte)50, Protocol.ChannelCount).ToArray();

        private byte[] BankReport(int bank)
        {
            var payload = new byte[Protocol.BankStatusSize];
            payload[0] = 1;
            for (var index = 0; index < Protocol.BankSize; index++)
            {
                var offset = 1 + index * 5;
                BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(offset), FreqHz[bank * Protocol.BankSize + index]);
                payload[offset + 4] = Duty[bank * Protocol.BankSize + index];
            }
            return payload;
        }

        public byte[] Get(byte reportId, int size)
        {
            BeforeGet?.Invoke(reportId);
            if (Unplugged)
                throw new IOException("device unplugged");
            if (reportId is >= Protocol.BankStatusReport and < Protocol.BankStatusReport + Protocol.BankCount)
                return BankReport(reportId - Protocol.BankStatusReport);
            return reportId switch
            {
                Protocol.IdentityReport => IdentityReport,
                Protocol.ConfigurationReport => Configuration,
                Protocol.MemoryReport => [1, 0xC0, 0xD4, 1, 0, 0, 0x20, 8, 0],
                Protocol.RuntimeReport => [1, 123, 0, 0, 0, 0x74, 0x59, 0, 0, 0x92, 4, 0x40, 0x1A],
                Protocol.PinsReport => InvalidPins ? new byte[28] :
                    [1, .. Enumerable.Range(0, 24).Select(pin => (byte)pin), 26, 27, 255],
                Protocol.ActionReport => Result,
                _ => Bank,
            };
        }

        public void Set(byte reportId, byte[] payload)
        {
            BeforeSet?.Invoke(payload);
            Sent.Add(payload);
            if (mode == "drop")
                return;
            var status = mode == "pending" ? (byte)6
                : mode == "fail-bank" && payload[2] == 3 ? (byte)2
                : (byte)0;
            if (payload[2] == Protocol.OpSetChannel && mode is "invalid-channel" or "invalid-applied")
            {
                status = 2;
                if (mode == "invalid-applied")
                {
                    FreqHz[payload[3]] = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(4));
                    Duty[payload[3]] = payload[8];
                }
            }
            Result = [1, payload[1], payload[2], status, 0, 0, 0, 0, 0, 0, 0, 0];
        }

        public void Dispose()
        {
            Closed = true;
            if (CloseError is not null) throw CloseError;
        }
    }

    internal static (FakeDevice, PicoPwmHid) Make(string mode = "ok")
    {
        var device = new FakeDevice(mode);
        long now = 0;
        return (device, new PicoPwmHid(() => device, _ => { }, () => now++));
    }

    internal static (FakeDevice, PicoPwmHid) Modern(string mode = "ok")
    {
        var (device, client) = Make(mode);
        device.IdentityReport = (byte[])Identity.Clone();
        device.IdentityReport[16] = 255;
        return (device, client);
    }
}
