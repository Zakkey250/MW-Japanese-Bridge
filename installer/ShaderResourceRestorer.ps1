[CmdletBinding()]
param()

Set-StrictMode -Version 2.0

if (-not ('NfsmwJapaneseBridge.ShaderResourceRestorer' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace NfsmwJapaneseBridge
{
    public sealed class ResourceItem
    {
        public string Name { get; set; }
        public ushort Language { get; set; }
        public byte[] Data { get; set; }
        public string Key { get { return Name + "|" + Language.ToString(); } }
    }

    public static class ShaderResourceRestorer
    {
        private const int RT_RCDATA = 10;
        private const uint LOAD_LIBRARY_AS_DATAFILE = 0x00000002;

        private delegate bool EnumResNameProc(IntPtr module, IntPtr type, IntPtr name, IntPtr parameter);
        private delegate bool EnumResLangProc(IntPtr module, IntPtr type, IntPtr name, ushort language, IntPtr parameter);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryEx(string fileName, IntPtr file, uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FreeLibrary(IntPtr module);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EnumResourceNames(IntPtr module, IntPtr type, EnumResNameProc callback, IntPtr parameter);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EnumResourceLanguages(IntPtr module, IntPtr type, IntPtr name, EnumResLangProc callback, IntPtr parameter);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr FindResourceEx(IntPtr module, IntPtr type, IntPtr name, ushort language);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LoadResource(IntPtr module, IntPtr resourceInfo);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LockResource(IntPtr resourceData);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint SizeofResource(IntPtr module, IntPtr resourceInfo);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr BeginUpdateResource(string fileName, [MarshalAs(UnmanagedType.Bool)] bool deleteExistingResources);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool UpdateResource(IntPtr update, IntPtr type, IntPtr name, ushort language, IntPtr data, uint size);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EndUpdateResource(IntPtr update, [MarshalAs(UnmanagedType.Bool)] bool discard);

        private static bool IsIntegerResource(IntPtr value)
        {
            return ((ulong)value.ToInt64() >> 16) == 0;
        }

        private static string ResourceName(IntPtr value)
        {
            return IsIntegerResource(value)
                ? "#" + ((ushort)value.ToInt64()).ToString()
                : Marshal.PtrToStringUni(value);
        }

        private static IntPtr AllocateResourceName(string name, out bool allocated)
        {
            if (name.StartsWith("#", StringComparison.Ordinal))
            {
                ushort id;
                if (!UInt16.TryParse(name.Substring(1), out id))
                    throw new InvalidDataException("Invalid integer resource name: " + name);
                allocated = false;
                return new IntPtr(id);
            }
            allocated = true;
            return Marshal.StringToHGlobalUni(name);
        }

        public static ResourceItem[] ReadRcData(string path)
        {
            if (!File.Exists(path)) throw new FileNotFoundException("Executable was not found", path);
            var result = new List<ResourceItem>();
            IntPtr module = LoadLibraryEx(path, IntPtr.Zero, LOAD_LIBRARY_AS_DATAFILE);
            if (module == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            try
            {
                EnumResNameProc nameCallback = delegate(IntPtr h, IntPtr type, IntPtr name, IntPtr parameter)
                {
                    string stableName = ResourceName(name);
                    EnumResLangProc languageCallback = delegate(IntPtr h2, IntPtr type2, IntPtr name2, ushort language, IntPtr parameter2)
                    {
                        IntPtr info = FindResourceEx(h2, new IntPtr(RT_RCDATA), name2, language);
                        if (info == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                        uint size = SizeofResource(h2, info);
                        IntPtr loaded = LoadResource(h2, info);
                        IntPtr bytes = loaded == IntPtr.Zero ? IntPtr.Zero : LockResource(loaded);
                        if (size != 0 && bytes == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                        byte[] data = new byte[size];
                        if (size != 0) Marshal.Copy(bytes, data, 0, checked((int)size));
                        result.Add(new ResourceItem { Name = stableName, Language = language, Data = data });
                        return true;
                    };
                    if (!EnumResourceLanguages(h, new IntPtr(RT_RCDATA), name, languageCallback, IntPtr.Zero))
                        throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                    return true;
                };
                if (!EnumResourceNames(module, new IntPtr(RT_RCDATA), nameCallback, IntPtr.Zero))
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }
            finally
            {
                FreeLibrary(module);
            }
            return result.OrderBy(x => x.Name, StringComparer.Ordinal).ThenBy(x => x.Language).ToArray();
        }

        public static string Fingerprint(string path)
        {
            ResourceItem[] items = ReadRcData(path);
            using (var aggregate = SHA256.Create())
            using (var itemHash = SHA256.Create())
            using (var stream = new MemoryStream())
            {
                foreach (ResourceItem item in items)
                {
                    string sha = BitConverter.ToString(itemHash.ComputeHash(item.Data)).Replace("-", "");
                    byte[] line = Encoding.UTF8.GetBytes(item.Name + "|" + item.Language.ToString() + "|" + item.Data.Length.ToString() + "|" + sha + "\n");
                    stream.Write(line, 0, line.Length);
                }
                return BitConverter.ToString(aggregate.ComputeHash(stream.ToArray())).Replace("-", "");
            }
        }

        public static void PatchCopy(string stockPath, string targetPath, string outputPath)
        {
            ResourceItem[] sourceItems = ReadRcData(stockPath);
            ResourceItem[] targetItems = ReadRcData(targetPath);
            if (sourceItems.Length == 0 || targetItems.Length == 0)
                throw new InvalidDataException("Both executables must contain RT_RCDATA resources.");

            File.Copy(targetPath, outputPath, true);
            IntPtr update = BeginUpdateResource(outputPath, false);
            if (update == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            bool completed = false;
            try
            {
                var sourceKeys = new HashSet<string>(sourceItems.Select(x => x.Key), StringComparer.Ordinal);
                foreach (ResourceItem item in targetItems.Where(x => !sourceKeys.Contains(x.Key)))
                    WriteResource(update, item, true);
                foreach (ResourceItem item in sourceItems)
                    WriteResource(update, item, false);
                if (!EndUpdateResource(update, false))
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
                completed = true;
            }
            finally
            {
                if (!completed) EndUpdateResource(update, true);
            }

            if (!String.Equals(Fingerprint(stockPath), Fingerprint(outputPath), StringComparison.Ordinal))
            {
                try { File.Delete(outputPath); } catch { }
                throw new InvalidDataException("Post-write verification failed: output RT_RCDATA differs from stock.");
            }
        }

        private static void WriteResource(IntPtr update, ResourceItem item, bool delete)
        {
            bool nameAllocated;
            IntPtr name = AllocateResourceName(item.Name, out nameAllocated);
            IntPtr data = IntPtr.Zero;
            try
            {
                uint size = delete ? 0u : checked((uint)item.Data.Length);
                if (!delete && size != 0)
                {
                    data = Marshal.AllocHGlobal(item.Data.Length);
                    Marshal.Copy(item.Data, 0, data, item.Data.Length);
                }
                if (!UpdateResource(update, new IntPtr(RT_RCDATA), name, item.Language, data, size))
                    throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }
            finally
            {
                if (data != IntPtr.Zero) Marshal.FreeHGlobal(data);
                if (nameAllocated) Marshal.FreeHGlobal(name);
            }
        }
    }
}
'@
}

function Get-NfsmwRcDataFingerprint([string]$Path) {
    return [NfsmwJapaneseBridge.ShaderResourceRestorer]::Fingerprint([IO.Path]::GetFullPath($Path))
}

function Restore-NfsmwStockShaderResources([string]$StockPath, [string]$TargetPath, [string]$OutputPath) {
    [NfsmwJapaneseBridge.ShaderResourceRestorer]::PatchCopy(
        [IO.Path]::GetFullPath($StockPath),
        [IO.Path]::GetFullPath($TargetPath),
        [IO.Path]::GetFullPath($OutputPath))
}
