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

    // Named pipe flags
    static readonly uint PIPE_ACCESS_INBOUND = 0x00000000;
    static readonly uint PIPE_ACCESS_OUTBOUND = 0x00000001;
    static readonly uint PIPE_TYPE_BYTE = 0x00000000;
    static readonly uint PIPE_READMODE_BYTE = 0x00000000;
    static readonly uint FILE_SHARE_READ = 0x00000001;
    static readonly uint FILE_SHARE_WRITE = 0x00000002;
    static readonly uint OPEN_EXISTING = 3;
    static readonly uint CREATE_NEW = 1;
    static readonly uint PIPE_WAIT = 0x00000000;
    static readonly uint PIPE_UNLIMITED_INSTANCES = 255;
    static readonly uint PIPE_NOWAIT = 0x00000001;

    delegate int CreatePseudoConsoleDelegate(
        short cx, short cy,
        IntPtr hInput, IntPtr hOutput,
        uint dwFlags, out IntPtr hPseudoConsole);

    delegate void ClosePseudoConsoleDelegate(IntPtr hPseudoConsole);

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
    static extern IntPtr CreateNamedPipe(
        string lpName, uint dwOpenMode,
        uint dwPipeMode, uint nMaxInstances,
        uint nOutBufferSize, uint nInBufferSize,
        uint nDefaultTimeOut, IntPtr lpSecurityAttributes);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool ConnectNamedPipe(IntPtr hNamedPipe, IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateFile(
        string lpFileName, uint dwDesiredAccess,
        uint dwShareMode, IntPtr lpSecurityAttributes,
        uint dwCreationDisposition, uint dwFlagsAndAttributes,
        IntPtr hTemplateFile);

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

    static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

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

        // Load ConPTY functions dynamically
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

        // Create named pipes with correct sharing modes
        // Input pipe: PTY reads from this end (FILE_SHARE_READ required)
        IntPtr hInputPipe = CreateNamedPipe(
            "\\\\.\\pipe\\pty_input",
            PIPE_ACCESS_INBOUND | FILE_SHARE_READ,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, IntPtr.Zero);

        if (hInputPipe == INVALID_HANDLE_VALUE)
        {
            Console.Error.WriteLine($"CreateNamedPipe(input) failed: {Marshal.GetLastWin32Error()}");
            Environment.Exit(10);
        }

        // Output pipe: PTY writes to this end (FILE_SHARE_WRITE required)
        IntPtr hOutputPipe = CreateNamedPipe(
            "\\\\.\\pipe\\pty_output",
            PIPE_ACCESS_OUTBOUND | FILE_SHARE_WRITE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, IntPtr.Zero);

        if (hOutputPipe == INVALID_HANDLE_VALUE)
        {
            CloseHandle(hInputPipe);
            Console.Error.WriteLine($"CreateNamedPipe(output) failed: {Marshal.GetLastWin32Error()}");
            Environment.Exit(11);
        }

        // Connect the other ends (client-side) of the pipes
        IntPtr hInputClient = CreateFile(
            "\\\\.\\pipe\\pty_input", 0, 0, IntPtr.Zero,
            OPEN_EXISTING, 0, IntPtr.Zero);

        if (hInputClient == INVALID_HANDLE_VALUE)
        {
            CloseHandle(hInputPipe);
            CloseHandle(hOutputPipe);
            Console.Error.WriteLine($"CreateFile(input) failed: {Marshal.GetLastWin32Error()}");
            Environment.Exit(12);
        }

        IntPtr hOutputClient = CreateFile(
            "\\\\.\\pipe\\pty_output", 0, 0, IntPtr.Zero,
            OPEN_EXISTING, 0, IntPtr.Zero);

        if (hOutputClient == INVALID_HANDLE_VALUE)
        {
            CloseHandle(hInputPipe);
            CloseHandle(hOutputPipe);
            CloseHandle(hInputClient);
            Console.Error.WriteLine($"CreateFile(output) failed: {Marshal.GetLastWin32Error()}");
            Environment.Exit(13);
        }

        // Connect named pipes
        if (!ConnectNamedPipe(hInputPipe, IntPtr.Zero))
        {
            int err = Marshal.GetLastWin32Error();
            if (err != 59) // ERROR_PIPE_CONNECTED
            {
                Cleanup(hInputPipe, hOutputPipe, hInputClient, hOutputClient);
                Console.Error.WriteLine($"ConnectNamedPipe(input) failed: {err}");
                Environment.Exit(14);
            }
        }

        if (!ConnectNamedPipe(hOutputPipe, IntPtr.Zero))
        {
            int err = Marshal.GetLastWin32Error();
            if (err != 59) // ERROR_PIPE_CONNECTED
            {
                Cleanup(hInputPipe, hOutputPipe, hInputClient, hOutputClient);
                Console.Error.WriteLine($"ConnectNamedPipe(output) failed: {err}");
                Environment.Exit(15);
            }
        }

        // Client handles must NOT be inherited
        SetHandleInformation(hInputClient, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hOutputClient, HANDLE_FLAG_INHERIT, 0);

        // Create pseudo-console
        // hInputPipe = read end (PTY reads input from here)
        // hOutputPipe = write end (PTY writes output here)
        IntPtr hPty = IntPtr.Zero;
        int ptyResult = createPty(120, 50, hInputPipe, hOutputPipe, 0, out hPty);
        if (ptyResult != 0)
        {
            Cleanup(hInputPipe, hOutputPipe, hInputClient, hOutputClient);
            Console.Error.WriteLine($"CreatePseudoConsole failed with NTSTATUS 0x{ptyResult:X8}");
            Environment.Exit(20);
        }

        // Duplicate PTY handle so it's inheritable
        IntPtr hProc = GetCurrentProcess();
        if (!DuplicateHandle(hProc, hPty, hProc, out IntPtr hPtyDup, 0, true, DUPLICATE_SAME_ACCESS))
        {
            closePty(hPty);
            Cleanup(hInputPipe, hOutputPipe, hInputClient, hOutputClient);
            Console.Error.WriteLine($"DuplicateHandle failed: {Marshal.GetLastWin32Error()}");
            Environment.Exit(30);
        }

        // Build attribute list
        IntPtr lpSize = IntPtr.Zero;
        if (!InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref lpSize))
        {
            if (Marshal.GetLastWin32Error() != 122) // ERROR_INSUFFICIENT_BUFFER
            {
                closePty(hPty);
                Cleanup(hInputPipe, hOutputPipe, hInputClient, hOutputClient);
                Console.Error.WriteLine($"InitProcThreadAttrList (query) failed: {Marshal.GetLastWin32Error()}");
                Environment.Exit(40);
            }
        }

        IntPtr lpAttributeList = Marshal.AllocHGlobal(lpSize.ToInt32());
        try
        {
            if (!InitializeProcThreadAttributeList(lpAttributeList, 1, 0, ref lpSize))
            {
                closePty(hPty);
                Cleanup(hInputPipe, hOutputPipe, hInputClient, hOutputClient);
                Console.Error.WriteLine($"InitProcThreadAttrList (init) failed: {Marshal.GetLastWin32Error()}");
                Environment.Exit(41);
            }

            if (!UpdateProcThreadAttribute(
                lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                hPtyDup, (uint)IntPtr.Size, IntPtr.Zero, IntPtr.Zero))
            {
                closePty(hPty);
                Cleanup(hInputPipe, hOutputPipe, hInputClient, hOutputClient);
                Console.Error.WriteLine($"UpdateProcThreadAttr failed: {Marshal.GetLastWin32Error()}");
                Environment.Exit(42);
            }

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
                Cleanup(hInputPipe, hOutputPipe, hInputClient, hOutputClient);
                Console.Error.WriteLine($"CreateProcess failed: {Marshal.GetLastWin32Error()}");
                Environment.Exit(50);
            }

            // Close handles not needed in parent
            CloseHandle(pi.hThread);
            CloseHandle(hPtyDup);
            CloseHandle(hInputPipe);
            CloseHandle(hOutputPipe);

            // Read all output from the client end of the output pipe
            string output = ReadAll(hOutputClient);

            // Close PTY to signal EOF to child
            closePty(hPty);

            // Wait for process to finish
            WaitForSingleObject(pi.hProcess, 0xFFFFFFFF);
            GetExitCodeProcess(pi.hProcess, out uint exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(hOutputClient);
            CloseHandle(hInputClient);

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

    static void Cleanup(params IntPtr[] handles)
    {
        foreach (var h in handles)
        {
            if (h != IntPtr.Zero && h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
    }
}
