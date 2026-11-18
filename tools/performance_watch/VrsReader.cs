using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

// Reads the port's published report only. No injection, remote calls or process writes.
public sealed class CpvrVrsReader : IDisposable
{
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] data, UIntPtr size, out UIntPtr read);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);
    IntPtr handle;
    long moduleBase;
    int moduleSize;
    uint sequenceRva, reportRva;
    public uint Sequence { get; private set; }

    // Resolve named exports from the loaded image, respecting ASLR and module bounds.
    public CpvrVrsReader(int pid)
    {
        if (!Environment.Is64BitProcess) throw new InvalidOperationException("Use 64-bit PowerShell.");
        using (var process = Process.GetProcessById(pid))
        {
            foreach (ProcessModule module in process.Modules)
                if (String.Equals(module.ModuleName, "cyberpunkvrport.dll", StringComparison.OrdinalIgnoreCase))
                { moduleBase = module.BaseAddress.ToInt64(); moduleSize = module.ModuleMemorySize; break; }
        }
        if (moduleBase == 0) throw new InvalidOperationException("VR port module not loaded yet.");
        handle = OpenProcess(0x1010, false, pid); // VM_READ | QUERY_LIMITED_INFORMATION
        if (handle == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            var dos = Read(0, 64);
            if (BitConverter.ToUInt16(dos, 0) != 0x5a4d) throw new InvalidOperationException("Invalid DOS image.");
            var nt = Read(BitConverter.ToUInt32(dos, 60), 144);
            if (BitConverter.ToUInt32(nt, 0) != 0x4550 || BitConverter.ToUInt16(nt, 24) != 0x20b)
                throw new InvalidOperationException("Expected PE64 image.");
            var exports = Read(BitConverter.ToUInt32(nt, 136), 40);
            uint functions = BitConverter.ToUInt32(exports, 28), names = BitConverter.ToUInt32(exports, 32);
            uint ordinals = BitConverter.ToUInt32(exports, 36), count = BitConverter.ToUInt32(exports, 24);
            uint functionCount = BitConverter.ToUInt32(exports, 20);
            if (count > 65536 || functionCount > 65536) throw new InvalidOperationException("Export count out of bounds.");
            var nameTable = Read(names, checked((int)count * 4));
            var ordinalTable = Read(ordinals, checked((int)count * 2));
            var functionTable = Read(functions, checked((int)functionCount * 4));
            for (int i = 0; i < count; ++i)
            {
                uint nameRva = BitConverter.ToUInt32(nameTable, i * 4);
                var nameBytes = Read(nameRva, checked((int)Math.Min(256L, moduleSize - (long)nameRva)));
                int end = Array.IndexOf(nameBytes, (byte)0);
                if (end < 0) continue;
                string name = Encoding.ASCII.GetString(nameBytes, 0, end);
                if (name != "CyberpunkVR_FixedVrsReportSeq" && name != "CyberpunkVR_FixedVrsReport") continue;
                int ordinal = BitConverter.ToUInt16(ordinalTable, i * 2);
                if (ordinal >= functionCount) throw new InvalidOperationException("Export ordinal out of bounds.");
                uint rva = BitConverter.ToUInt32(functionTable, ordinal * 4);
                if (name.EndsWith("Seq", StringComparison.Ordinal)) sequenceRva = rva; else reportRva = rva;
            }
            if (sequenceRva == 0 || reportRva == 0) throw new InvalidOperationException("VRS report exports unavailable in this DLL.");
        }
        catch { Dispose(); throw; }
    }

    // Copy only within this loaded module; partial reads and process exit are explicit failures.
    byte[] Read(uint rva, int bytes)
    {
        if (bytes <= 0 || rva >= moduleSize || (long)rva + bytes > moduleSize)
            throw new InvalidOperationException("Read outside module bounds.");
        byte[] data = new byte[bytes]; UIntPtr read;
        if (!ReadProcessMemory(handle, new IntPtr(moduleBase + rva), data, new UIntPtr((uint)bytes), out read) || read.ToUInt64() != (ulong)bytes)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Unable to read VRS report.");
        return data;
    }

    // Match the producer's odd/even sequence protocol. Never return a torn JSON report.
    public string Snapshot()
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            uint before = BitConverter.ToUInt32(Read(sequenceRva, 4), 0);
            if ((before & 1) != 0) continue;
            byte[] bytes = Read(reportRva, 1024);
            uint after = BitConverter.ToUInt32(Read(sequenceRva, 4), 0);
            if (before != after || (after & 1) != 0) continue;
            int end = Array.IndexOf(bytes, (byte)0);
            if (end < 0) throw new InvalidOperationException("Unterminated report.");
            Sequence = after;
            return Encoding.UTF8.GetString(bytes, 0, end);
        }
        throw new InvalidOperationException("VRS report busy; retry next sample.");
    }

    // Release the read-only OS handle when collection ends or the module is re-resolved.
    public void Dispose()
    {
        if (handle != IntPtr.Zero) { CloseHandle(handle); handle = IntPtr.Zero; }
    }
}
