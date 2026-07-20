using System;
using System.Runtime.InteropServices;
using System.Text;

class Program
{
    static readonly uint DUPLICATE_SAME_ACCESS = 0x2;
    static readonly uint HANDLE_FLAG_INHERIT = 0x1;
    static readonly uint CREATE_NO_WINDOW = 0x08000000;
    static readonly uint EXTENDED_STARTUPINFO_PRESENT = 0x00080000;
    static readonly ulong PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE = 0x00020016;

    delegate int CreatePseudoConsoleDelegate(
        short cx, short cy,
        IntPtr hInput, IntPtr hOutput,
        uint dwFlags, out IntPtr hPseudoConsole);

    delegate void ClosePseudoConsoleDelegate(IntPtr hPseudoConsole);

    [StructLayout(LayoutKind.Sequential)]
    struct SECURITY_ATTRIBUTES
    {
        public int nLength;
        public IntPtr lpSecurityDescriptor;
        public bool bInheritHandle;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct STARTUPINFOEX
    {
        public STARTUPINFO StartupInfo;
        public IntPtr lpAttributeList;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct STARTUPINFO
    {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX;
        public int dwY;
        public int dwXSize;
        public int dwYSize;
        public int dwXCountChars;
        public int dwYCountChars;
        public int dwFillAttribute;
        public int dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION
    {
        public IntPtr hProcess;
        public IntPtr hThread;
        public int dwProcessId;
        public int dwThreadId;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CreatePipe(
        out IntPtr hReadPipe, out IntPtr hWritePipe,
        ref SECURITY_ATTRIBUTES lpSecurityAttributes, uint nSize);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetHandleInformation(IntPtr hObject, uint dwMask, uint dwFlags);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DuplicateHandle(
        IntPtr hSourceProcessHandle, IntPtr hSourceHandle,
        IntPtr hTargetProcessHandle, out IntPtr lpTargetHandle,
        uint dwDesiredAccess, [MarshalAs(UnmanagedType.Bool)] bool bInheritHandle,
        uint dwOptions);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr GetCurrentProcess();

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool InitializeProcThreadAttributeList(
        IntPtr lpAttributeList, int dwAttributeCount, int dwFlags, ref IntPtr lpSize);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool UpdateProcThreadAttribute(
        IntPtr lpAttributeList, uint dwFlags, ulong attribute,
        IntPtr lpValue, uint cbSize, IntPtr lpPreviousValue, IntPtr lpReturnSize);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool DeleteProcThreadAttributeList(IntPtr lpAttributeList);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool CreateProcess(
        string lpApplicationName, string lpCommandLine,
        IntPtr lpProcessAttributes, IntPtr lpThreadAttributes,
        [MarshalAs(UnmanagedType.Bool)] bool bInheritHandles,
        uint dwCreationFlags, IntPtr lpEnvironment,
        string lpCurrentDirectory,
        [In] ref STARTUPINFOEX lpStartupInfo,
        out PROCESS_INFORMATION lpProcessInformation);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetExitCodeProcess(IntPtr hProcess, out uint lpExitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool ReadFile(
        IntPtr hFile, [Out] byte[] lpBuffer,
        uint nNumberOfBytesToRead, out uint lpNumberOfBytesRead,
        IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr GetModuleHandle(string lpModuleName);

    static void Main(string[] args)
    {
        if (args.Length < 1)
        {
            Console.Error.WriteLine("Usage: ConptyCapture.exe <exe-path> [args...]");
            Environment.Exit(1);
        }

        string exePath = args[0];
        string commandLine = $"\"{exePath}\"";
        if (args.Length > 1)
        {
            commandLine += " " + string.Join(" ", args, 1, args.Length - 1);
        }

        // Load ConPTY functions dynamically (Windows 10 1803+)
        IntPtr hKernel32 = GetModuleHandle("kernel32.dll");
        IntPtr pCreatePty = GetProcAddress(hKernel32, "CreatePseudoConsole");
        IntPtr pClosePty = GetProcAddress(hKernel32, "ClosePseudoConsole");

        if (pCreatePty == IntPtr.Zero || pClosePty == IntPtr.Zero)
        {
            Console.Error.WriteLine("CreatePseudoConsole not available (need Windows 10 1803+)");
            Environment.Exit(1);
        }

        CreatePseudoConsoleDelegate createPty =
            (CreatePseudoConsoleDelegate)Marshal.GetDelegateForFunctionPointer(
                pCreatePty, typeof(CreatePseudoConsoleDelegate));
        ClosePseudoConsoleDelegate closePty =
            (ClosePseudoConsoleDelegate)Marshal.GetDelegateForFunctionPointer(
                pClosePty, typeof(ClosePseudoConsoleDelegate));

        // 1. Create pipe for capturing output
        var sa = new SECURITY_ATTRIBUTES
        {
            nLength = Marshal.SizeOf<SECURITY_ATTRIBUTES>(),
            lpSecurityDescriptor = IntPtr.Zero,
            bInheritHandle = false
        };

        if (!CreatePipe(out IntPtr hReadPipe, out IntPtr hWritePipe, ref sa, 0))
        {
            Fail("CreatePipe failed", 1);
        }

        // Read end must NOT be inherited by child
        if (!SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0))
        {
            Fail("SetHandleInformation(read) failed", 2);
        }

        // 2. Create pseudo-console (120x50 cells)
        IntPtr hPty = IntPtr.Zero;
        int ptyResult = createPty(120, 50, IntPtr.Zero, hWritePipe, 0, out hPty);
        if (ptyResult != 0)
        {
            Fail($"CreatePseudoConsole failed with error {ptyResult}", 3);
        }

        // 3. Duplicate PTY handle so it's inheritable
        IntPtr hProc = GetCurrentProcess();
        if (!DuplicateHandle(hProc, hPty, hProc, out IntPtr hPtyDup, 0, true, DUPLICATE_SAME_ACCESS))
        {
            closePty(hPty);
            Fail("DuplicateHandle failed", 4);
        }

        // 4. Build PROC_THREAD_ATTRIBUTE_LIST with pseudo-console attribute
        IntPtr lpSize = IntPtr.Zero;
        if (!InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref lpSize))
        {
            if (Marshal.GetLastWin32Error() != 122) // ERROR_INSUFFICIENT_BUFFER
            {
                closePty(hPty);
                Fail("InitializeProcThreadAttributeList (query) failed", 5);
            }
        }

        IntPtr lpAttributeList = Marshal.AllocHGlobal(lpSize.ToInt32());
        try
        {
            if (!InitializeProcThreadAttributeList(lpAttributeList, 1, 0, ref lpSize))
            {
                closePty(hPty);
                Fail("InitializeProcThreadAttributeList (init) failed", 6);
            }

            if (!UpdateProcThreadAttribute(
                lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                hPtyDup, (uint)IntPtr.Size, IntPtr.Zero, IntPtr.Zero))
            {
                closePty(hPty);
                Fail("UpdateProcThreadAttribute failed", 7);
            }

            // 5. Create child process attached to pseudo-console
            var si = new STARTUPINFOEX
            {
                StartupInfo = new STARTUPINFO
                {
                    cb = Marshal.SizeOf<STARTUPINFOEX>()
                },
                lpAttributeList = lpAttributeList
            };

            if (!CreateProcess(null, commandLine, IntPtr.Zero, IntPtr.Zero,
                true, CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                IntPtr.Zero, null, ref si, out PROCESS_INFORMATION pi))
            {
                closePty(hPty);
                Fail("CreateProcess failed", 8);
            }

            // Close handles we don't need in parent
            CloseHandle(pi.hThread);
            CloseHandle(hPtyDup);
            CloseHandle(hWritePipe);

            // 6. Read all output from the pipe (child writes via PTY -> pipe)
            string output = ReadAll(hReadPipe);

            // 7. Close PTY to signal EOF, wait for process
            closePty(hPty);
            WaitForSingleObject(pi.hProcess, 0xFFFFFFFF); // INFINITE
            GetExitCodeProcess(pi.hProcess, out uint exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(hReadPipe);

            // Write captured output to stdout
            Console.Write(output);
            Environment.Exit((int)exitCode);
        }
        finally
        {
            DeleteProcThreadAttributeList(lpAttributeList);
            Marshal.FreeHGlobal(lpAttributeList);
        }
    }

    static string ReadAll(IntPtr hReadPipe)
    {
        var sb = new StringBuilder();
        byte[] buffer = new byte[4096];
        uint bytesRead;

        while (ReadFile(hReadPipe, buffer, (uint)buffer.Length, out bytesRead, IntPtr.Zero))
        {
            if (bytesRead == 0) break;
            sb.Append(Encoding.UTF8.GetString(buffer, 0, (int)bytesRead));
        }
        return sb.ToString();
    }

    static void Fail(string msg, int code)
    {
        Console.Error.WriteLine($"ERROR: {msg} (Win32: {Marshal.GetLastWin32Error()})");
        Environment.Exit(code);
    }
}
