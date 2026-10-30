// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

using System.Security.Cryptography;
using System.Text;

namespace PicoPwm.Host;

public sealed record BoardInfo(string Id, string ProductName, string? SerialNumber, string DevicePath)
{
    public static BoardInfo Create(string? product, string? serial, string path)
    {
        serial = string.IsNullOrEmpty(serial) ? null : serial;
        var id = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(
            serial is null ? $"path:{path}" : $"serial:{serial}"))).ToLowerInvariant();
        return new(id, string.IsNullOrEmpty(product) ? "PicoPWM" : product, serial, path);
    }
}

public sealed record BoardScan(BoardInfo[] Boards, string? DefaultBoardId);

/// <summary>Page-local selection resolves to a shared client pinned to one discovered board.</summary>
public sealed class BoardRegistry(Func<BoardInfo[]> discover, Func<BoardInfo, PicoPwmHid> factory,
    string? serial = null, string? path = null) : IDisposable
{
    private readonly object gate = new();
    private readonly Dictionary<string, BoardInfo> known = new();
    private readonly Dictionary<string, PicoPwmHid> clients = new();

    public BoardScan Boards()
    {
        lock (gate)
        {
            var boards = discover();
            if (boards.Select(board => board.Id).Distinct().Count() != boards.Length)
                throw new DeviceException("duplicate PicoPWM board identity; board serials must be unique");
            foreach (var board in boards) known[board.Id] = board;
            var preferred = boards.FirstOrDefault(board => (serial is not null && board.SerialNumber == serial)
                || (path is not null && board.DevicePath == path));
            return new(boards, preferred?.Id ?? (serial is null && path is null ? boards.FirstOrDefault()?.Id : null));
        }
    }

    public PicoPwmHid Client(string? id)
    {
        lock (gate)
        {
            if (id is null || !known.TryGetValue(id, out var board))
                throw new DeviceException("unknown board; rescan and select a listed board");
            if (!clients.TryGetValue(id, out var client))
                clients[id] = client = factory(board);
            return client;
        }
    }

    public void Dispose()
    {
        lock (gate)
        {
            foreach (var client in clients.Values) client.Close();
            clients.Clear();
        }
    }
}
