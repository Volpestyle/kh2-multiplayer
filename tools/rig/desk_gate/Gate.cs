// Native side of the gated desk agent (VUH-2339), loaded by agent.ps1 with Add-Type -Path. Windows
// PowerShell 5.1 compiles it with C# 5: no interpolated strings, expression bodies or ?. operators.
//
// Physical idle: low-level keyboard/mouse hooks see every event with its injected flag, so agent
// input (SendInput, SendKeys, SetCursorPos-driven clicks) never counts as a person. GetLastInputInfo
// counts all input; input it reports that the hooks did not see (a removed hook, a lock-screen
// desktop) counts as physical. A changed XInput packet number on a connected pad counts as physical.
// Only times are kept: never keys, text, pointer positions or window titles.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace Kh2Desk {
public static class Gate {
    delegate IntPtr HookProc(int code, IntPtr wParam, IntPtr lParam);
    delegate bool EnumWindowsProc(IntPtr window, IntPtr param);

    static long lastPhysical, lastInjected, lastUnattributed, hooksAt;
    static volatile bool hooksOk;
    static HookProc kbProc, msProc;  // kept alive for the hooks' lifetime
    static readonly long[] padPose = new long[4];
    static readonly bool[] padSeen = new bool[4];

    public static long Now() { return (long)GetTickCount64(); }
    public static bool HooksOk { get { return hooksOk; } }
    public static long HooksAt { get { return Interlocked.Read(ref hooksAt); } }
    public static long LastInjected { get { return Interlocked.Read(ref lastInjected); } }

    // Latest input that may have been a person: physical, pad or unattributed.
    public static long LastHuman {
        get { return Math.Max(Interlocked.Read(ref lastPhysical), Interlocked.Read(ref lastUnattributed)); }
    }

    public static void Start() {
        Thread t = new Thread(Pump);
        t.IsBackground = true;
        t.Start();
        for (int i = 0; i < 50 && HooksAt == 0; i++) Thread.Sleep(20);
    }

    static void Pump() {
        kbProc = Keyboard;
        msProc = Mouse;
        IntPtr module = GetModuleHandle(null);
        IntPtr kb = SetWindowsHookEx(13, kbProc, module, 0);  // WH_KEYBOARD_LL
        IntPtr ms = SetWindowsHookEx(14, msProc, module, 0);  // WH_MOUSE_LL
        hooksOk = kb != IntPtr.Zero && ms != IntPtr.Zero;
        Interlocked.Exchange(ref hooksAt, Now());
        MSG msg;
        while (GetMessage(out msg, IntPtr.Zero, 0, 0) > 0) { TranslateMessage(ref msg); DispatchMessage(ref msg); }
        hooksOk = false;
    }

    static void Mark(bool injected) {
        if (injected) Interlocked.Exchange(ref lastInjected, Now());
        else Interlocked.Exchange(ref lastPhysical, Now());
    }

    static IntPtr Keyboard(int code, IntPtr wParam, IntPtr lParam) {
        if (code >= 0) Mark((Marshal.ReadInt32(lParam, 8) & 0x12) != 0);   // LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED
        return CallNextHookEx(IntPtr.Zero, code, wParam, lParam);
    }

    static IntPtr Mouse(int code, IntPtr wParam, IntPtr lParam) {
        if (code >= 0) Mark((Marshal.ReadInt32(lParam, 12) & 0x03) != 0);  // LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED
        return CallNextHookEx(IntPtr.Zero, code, wParam, lParam);
    }

    // Called by the agent loop about every 100 ms; returns LastHuman.
    public static long Poll() {
        long now = Now();
        LASTINPUTINFO info = new LASTINPUTINFO();
        info.cbSize = (uint)Marshal.SizeOf(typeof(LASTINPUTINFO));
        if (GetLastInputInfo(ref info)) {
            long seen = Math.Max(Interlocked.Read(ref lastPhysical), Interlocked.Read(ref lastInjected));
            long any = now - (long)unchecked((uint)now - info.dwTime);
            if (any > seen + 250 && any > Interlocked.Read(ref lastUnattributed)) Interlocked.Exchange(ref lastUnattributed, any);
        } else {
            Interlocked.Exchange(ref lastUnattributed, now);
        }
        for (uint i = 0; i < 4; i++) {
            XINPUT_STATE state;
            if (XInputGetState(i, out state) == 0) {
                long pose = PadPose(state);
                if (padSeen[i] && pose != padPose[i]) Interlocked.Exchange(ref lastPhysical, now);
                padPose[i] = pose;
                padSeen[i] = true;
            } else {
                padSeen[i] = false;
            }
        }
        return LastHuman;
    }

    // Buttons, triggers past their threshold and sticks past the XInput deadzone, so a drifting stick
    // (which bumps the packet number) does not read as a person.
    static long PadPose(XINPUT_STATE s) {
        long pose = s.wButtons;
        if (s.bLeftTrigger > 30) pose |= 1L << 16;
        if (s.bRightTrigger > 30) pose |= 1L << 17;
        int bit = 18;
        foreach (short axis in new short[] { s.sThumbLX, s.sThumbLY, s.sThumbRX, s.sThumbRY }) {
            int dead = bit < 22 ? 7849 : 8689;
            if (axis > dead) pose |= 1L << bit;
            if (axis < -dead) pose |= 1L << (bit + 1);
            bit += 2;
        }
        return pose;
    }

    // A job object holds an admitted job's whole process tree, including processes it starts later and
    // games kh2ctl creates before recording them, so reclaim can end exactly that tree. KILL_ON_JOB_CLOSE
    // ends the tree if the agent itself dies, so no job ever runs without its watchdog.
    public static IntPtr NewJob() {
        IntPtr job = CreateJobObject(IntPtr.Zero, null);
        if (job == IntPtr.Zero) return job;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
        info.BasicLimitInformation.LimitFlags = 0x2000;  // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        int size = Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
        IntPtr buffer = Marshal.AllocHGlobal(size);
        try {
            Marshal.StructureToPtr(info, buffer, false);
            if (!SetInformationJobObject(job, 9, buffer, (uint)size)) { CloseHandle(job); return IntPtr.Zero; }  // ExtendedLimitInformation
        } finally { Marshal.FreeHGlobal(buffer); }
        return job;
    }

    public static bool Assign(IntPtr job, int pid) {
        IntPtr h = OpenProcess(0x0100 | 0x0001, false, (uint)pid);  // PROCESS_SET_QUOTA | PROCESS_TERMINATE
        if (h == IntPtr.Zero) return false;
        try { return AssignProcessToJobObject(job, h); } finally { CloseHandle(h); }
    }

    // Live processes in the job, or -1 when it can't be read.
    public static int JobProcesses(IntPtr job) {
        // JOBOBJECT_BASIC_ACCOUNTING_INFORMATION: four LARGE_INTEGER times, then page faults, total,
        // active and terminated process counts (48 bytes; ActiveProcesses at 40).
        GCHandle pin = GCHandle.Alloc(new long[6], GCHandleType.Pinned);
        try {
            uint returned;
            if (!QueryInformationJobObject(job, 1, pin.AddrOfPinnedObject(), 48, out returned)) return -1;  // BasicAccountingInformation
            return Marshal.ReadInt32(pin.AddrOfPinnedObject(), 40);  // ActiveProcesses
        } finally { pin.Free(); }
    }

    public static bool EndJob(IntPtr job, int waitMs) {
        TerminateJobObject(job, 1);
        long deadline = Now() + waitMs;
        while (JobProcesses(job) != 0) {
            if (Now() > deadline) return false;
            Thread.Sleep(10);
        }
        return true;
    }

    public static void CloseJob(IntPtr job) { if (job != IntPtr.Zero) CloseHandle(job); }

    public static bool InJob(IntPtr job, int pid) {
        if (job == IntPtr.Zero) return false;
        IntPtr h = OpenProcess(0x1000, false, (uint)pid);
        if (h == IntPtr.Zero) return false;
        try { bool result; return IsProcessInJob(h, job, out result) && result; } finally { CloseHandle(h); }
    }

    public static int ForegroundPid() {
        uint pid;
        IntPtr window = GetForegroundWindow();
        if (window == IntPtr.Zero) return 0;
        GetWindowThreadProcessId(window, out pid);
        return (int)pid;
    }

    public static int ActiveConsoleSession() { return unchecked((int)WTSGetActiveConsoleSessionId()); }

    // "Default" when the user's desktop takes input; "Winlogon" (or "error:<code>" when it can't be
    // opened, as while the secure desktop is up) when locked.
    public static string InputDesktop() {
        IntPtr desk = OpenInputDesktop(0, false, 0x0001);  // DESKTOP_READOBJECTS
        if (desk == IntPtr.Zero) return "error:" + Marshal.GetLastWin32Error();
        try {
            byte[] buffer = new byte[256];
            uint needed;
            if (!GetUserObjectInformation(desk, 2, buffer, (uint)buffer.Length, out needed)) return "error:" + Marshal.GetLastWin32Error();  // UOI_NAME
            return Encoding.Unicode.GetString(buffer, 0, (int)Math.Max(0, needed - 2)).TrimEnd('\0');
        } finally { CloseDesktop(desk); }
    }

    // QUNS_BUSY (2), QUNS_RUNNING_D3D_FULL_SCREEN (3) and QUNS_PRESENTATION_MODE (4) mean a fullscreen app.
    public static int NotificationState() {
        int state;
        return SHQueryUserNotificationState(out state) == 0 ? state : -1;
    }

    // Name is the image name without its extension, like Process.ProcessName.
    public class Proc { public int Pid; public int Parent; public long Created; public string Path; public string Name; }

    public static List<Proc> Snapshot() {
        List<Proc> rows = Entries();
        foreach (Proc p in rows) Identity(p.Pid, out p.Created, out p.Path);
        return rows;
    }

    // Processes that own a visible unowned top-level window (what Process.MainWindowHandle finds), from one
    // EnumWindows pass. Only these are opened for their path: Get-Process reading MainWindowHandle and Path
    // for every process cost PC1 (~900 processes) seconds of CPU a minute.
    public static List<Proc> Windowed() {
        Dictionary<int, bool> pids = new Dictionary<int, bool>();
        EnumWindowsProc each = delegate(IntPtr w, IntPtr l) {
            uint pid;
            if (IsWindowVisible(w) && GetWindow(w, 4) == IntPtr.Zero && GetWindowThreadProcessId(w, out pid) != 0) pids[(int)pid] = true;  // GW_OWNER
            return true;
        };
        if (!EnumWindows(each, IntPtr.Zero)) throw new InvalidOperationException("EnumWindows failed");
        List<Proc> rows = new List<Proc>();
        foreach (Proc p in Entries()) if (pids.ContainsKey(p.Pid)) { Identity(p.Pid, out p.Created, out p.Path); rows.Add(p); }
        return rows;
    }

    // How many processes run an image from under the directory (the KH2 install).
    public static int Under(string directory) {
        string prefix = directory.TrimEnd('\\') + "\\";
        int count = 0;
        foreach (Proc p in Snapshot()) if (p.Path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) count++;
        return count;
    }

    // Throws rather than return an empty list, which would read as "nothing running": a real snapshot
    // always holds at least System and the agent.
    static List<Proc> Entries() {
        List<Proc> rows = new List<Proc>();
        IntPtr snap = CreateToolhelp32Snapshot(2, 0);  // TH32CS_SNAPPROCESS
        if (snap == new IntPtr(-1)) throw new InvalidOperationException("process snapshot failed");
        try {
            PROCESSENTRY32W entry = new PROCESSENTRY32W();
            entry.dwSize = (uint)Marshal.SizeOf(typeof(PROCESSENTRY32W));
            for (bool ok = Process32FirstW(snap, ref entry); ok; ok = Process32NextW(snap, ref entry)) {
                Proc p = new Proc();
                p.Pid = (int)entry.th32ProcessID;
                p.Parent = (int)entry.th32ParentProcessID;
                p.Name = System.IO.Path.GetFileNameWithoutExtension(entry.szExeFile);
                p.Path = "";
                rows.Add(p);
            }
        } finally { CloseHandle(snap); }
        if (rows.Count < 2) throw new InvalidOperationException("process snapshot came back empty");
        return rows;
    }

    static void Identity(int pid, out long created, out string path) {
        created = 0;
        path = "";
        IntPtr h = OpenProcess(0x1000, false, (uint)pid);  // PROCESS_QUERY_LIMITED_INFORMATION
        if (h == IntPtr.Zero) return;
        try {
            long c, e, k, u;
            if (GetProcessTimes(h, out c, out e, out k, out u)) created = c;
            StringBuilder sb = new StringBuilder(1024);
            uint size = (uint)sb.Capacity;
            if (QueryFullProcessImageNameW(h, 0, sb, ref size)) path = sb.ToString();
        } finally { CloseHandle(h); }
    }

    // The process and its live descendants, leaves first. A child must be newer than its parent,
    // so a reused parent PID never adopts an unrelated process.
    public static List<Proc> Tree(List<Proc> all, int root, long rootCreated) {
        List<Proc> tree = new List<Proc>();
        foreach (Proc p in all) if (p.Pid == root && p.Created == rootCreated) tree.Add(p);
        for (int i = 0; i < tree.Count; i++)
            foreach (Proc p in all)
                if (p.Parent == tree[i].Pid && p.Pid != tree[i].Pid && p.Created >= tree[i].Created && !tree.Contains(p)) tree.Add(p);
        tree.Reverse();
        return tree;
    }

    // Terminates pid only while it still has the expected creation time. True when it has exited.
    public static bool Terminate(int pid, long created) {
        IntPtr h = OpenProcess(0x0001 | 0x1000 | 0x00100000, false, (uint)pid);  // TERMINATE | QUERY_LIMITED | SYNCHRONIZE
        if (h == IntPtr.Zero) return Marshal.GetLastWin32Error() == 87;
        try {
            long c, e, k, u;
            if (!GetProcessTimes(h, out c, out e, out k, out u) || c != created) return true;  // gone or reused
            TerminateProcess(h, 1);
            return WaitForSingleObject(h, 1000) == 0;
        } finally { CloseHandle(h); }
    }

    // A process that exists but can't be opened counts as alive, so cleanup never assumes an exit.
    public static bool Alive(int pid, long created) {
        IntPtr h = OpenProcess(0x1000 | 0x00100000, false, (uint)pid);
        if (h == IntPtr.Zero) return Marshal.GetLastWin32Error() != 87;  // ERROR_INVALID_PARAMETER: no such process
        try {
            long c, e, k, u;
            if (!GetProcessTimes(h, out c, out e, out k, out u) || c != created) return false;
            return WaitForSingleObject(h, 0) != 0;
        } finally { CloseHandle(h); }
    }

    // Only a process opened and matched by creation time, and not yet exited, counts: unlike Alive, an
    // unreadable process proves nothing. Used where ownership must be proven (the busy-state exemption).
    public static bool ProvenAlive(int pid, long created) {
        if (pid <= 0) return false;
        IntPtr h = OpenProcess(0x1000 | 0x00100000, false, (uint)pid);
        if (h == IntPtr.Zero) return false;
        try {
            long c, e, k, u;
            return GetProcessTimes(h, out c, out e, out k, out u) && c == created && WaitForSingleObject(h, 0) != 0;
        } finally { CloseHandle(h); }
    }

    // True when the window rectangle covers the whole monitor rectangle (fullscreen or borderless fullscreen).
    public static bool Covers(int left, int top, int right, int bottom, int monLeft, int monTop, int monRight, int monBottom) {
        return monRight > monLeft && monBottom > monTop && left <= monLeft && top <= monTop && right >= monRight && bottom >= monBottom;
    }

    // A visible, uncloaked, unminimized top-level window that covers its monitor, owned popups included (each is
    // attributed by its own pid: a foreign fullscreen popup over the rig's game must block the busy exemption, Sage on
    // owned-busy r2). The shell's desktop windows (Progman, WorkerW) cover every monitor and are left out.
    public class Cover { public int Pid; public string Class; public long Monitor; public bool Owned; }

    // Whether a top-level window is a covering candidate before its geometry is checked. Ownership is reported, never
    // a reason to skip.
    public static bool CoverCandidate(bool visible, bool iconic, bool cloaked, bool hasOwner, string className) {
        if (!visible || iconic || cloaked) return false;
        return className != "Progman" && className != "WorkerW";
    }

    // What Covering needs to know about one top-level window, gathered without any decision (see Facts).
    public class WindowFacts {
        public bool Visible, Iconic, Cloaked, HasOwner, HasRect, HasMonitor;
        public string Class = "";
        public int Left, Top, Right, Bottom, MonLeft, MonTop, MonRight, MonBottom, Pid;
        public long Monitor;
    }

    // The whole per-window decision, pure so it can be tested without real windows: a covering row, or null.
    public static Cover CoverFrom(WindowFacts f) {
        if (f == null || !CoverCandidate(f.Visible, f.Iconic, f.Cloaked, f.HasOwner, f.Class)) return null;
        if (!f.HasRect || !f.HasMonitor) return null;
        if (!Covers(f.Left, f.Top, f.Right, f.Bottom, f.MonLeft, f.MonTop, f.MonRight, f.MonBottom)) return null;
        Cover c = new Cover();
        c.Pid = f.Pid; c.Class = f.Class; c.Monitor = f.Monitor; c.Owned = f.HasOwner;
        return c;
    }

    // Reads one window's facts. An invisible window stops at Visible = false, which CoverFrom rejects anyway.
    static WindowFacts Facts(IntPtr w) {
        WindowFacts f = new WindowFacts();
        f.Visible = IsWindowVisible(w);
        if (!f.Visible) return f;
        f.Iconic = IsIconic(w);
        int cloaked;
        f.Cloaked = DwmGetWindowAttribute(w, 14, out cloaked, 4) == 0 && cloaked != 0;   // DWMWA_CLOAKED
        f.HasOwner = GetWindow(w, 4) != IntPtr.Zero;   // GW_OWNER
        StringBuilder cls = new StringBuilder(256);
        GetClassName(w, cls, cls.Capacity);
        f.Class = cls.ToString();
        RECT r;
        f.HasRect = GetWindowRect(w, out r);
        f.Left = r.Left; f.Top = r.Top; f.Right = r.Right; f.Bottom = r.Bottom;
        IntPtr monitor = MonitorFromWindow(w, 0);   // MONITOR_DEFAULTTONULL
        MONITORINFO mi = new MONITORINFO();
        mi.cbSize = (uint)Marshal.SizeOf(typeof(MONITORINFO));
        f.HasMonitor = monitor != IntPtr.Zero && GetMonitorInfo(monitor, ref mi);
        f.Monitor = monitor.ToInt64();
        f.MonLeft = mi.Monitor.Left; f.MonTop = mi.Monitor.Top; f.MonRight = mi.Monitor.Right; f.MonBottom = mi.Monitor.Bottom;
        uint pid;
        GetWindowThreadProcessId(w, out pid);
        f.Pid = (int)pid;
        return f;
    }

    // The covering rows for a set of windows' facts: every window CoverFrom accepts, owned popups included.
    public static List<Cover> CoversFrom(List<WindowFacts> windows) {
        List<Cover> rows = new List<Cover>();
        foreach (WindowFacts f in windows) {
            Cover c = CoverFrom(f);
            if (c != null) rows.Add(c);
        }
        return rows;
    }

    public static List<Cover> Covering() {
        List<WindowFacts> windows = new List<WindowFacts>();
        EnumWindowsProc each = delegate(IntPtr w, IntPtr l) { windows.Add(Facts(w)); return true; };
        if (!EnumWindows(each, IntPtr.Zero)) throw new InvalidOperationException("EnumWindows failed");
        return CoversFrom(windows);
    }

    [StructLayout(LayoutKind.Sequential)] struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct MONITORINFO { public uint cbSize; public RECT Monitor; public RECT Work; public uint Flags; }
    [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
    [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam; public uint time; public int x; public int y; }
    [StructLayout(LayoutKind.Sequential)] struct XINPUT_STATE { public uint dwPacketNumber; public ushort wButtons; public byte bLeftTrigger; public byte bRightTrigger; public short sThumbLX; public short sThumbLY; public short sThumbRX; public short sThumbRY; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] struct PROCESSENTRY32W {
        public uint dwSize; public uint cntUsage; public uint th32ProcessID; public IntPtr th32DefaultHeapID; public uint th32ModuleID;
        public uint cntThreads; public uint th32ParentProcessID; public int pcPriClassBase; public uint dwFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szExeFile;
    }

    [StructLayout(LayoutKind.Sequential)] struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
        public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags; public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit; public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)] struct IO_COUNTERS { public ulong a, b, c, d, e, f; }
    [StructLayout(LayoutKind.Sequential)] struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation; public IO_COUNTERS IoInfo; public UIntPtr ProcessMemoryLimit;
        public UIntPtr JobMemoryLimit; public UIntPtr PeakProcessMemoryUsed; public UIntPtr PeakJobMemoryUsed;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll")] static extern bool SetInformationJobObject(IntPtr job, int infoClass, IntPtr info, uint length);
    [DllImport("kernel32.dll")] static extern bool QueryInformationJobObject(IntPtr job, int infoClass, IntPtr info, uint length, out uint returned);
    [DllImport("kernel32.dll")] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll")] static extern bool TerminateJobObject(IntPtr job, uint code);
    [DllImport("kernel32.dll")] static extern bool IsProcessInJob(IntPtr process, IntPtr job, out bool result);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc proc, IntPtr param);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetMonitorInfoW")] static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFO info);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "GetClassNameW")] static extern int GetClassName(IntPtr window, StringBuilder name, int capacity);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int size);
    [DllImport("kernel32.dll")] static extern ulong GetTickCount64();
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr SetWindowsHookEx(int id, HookProc proc, IntPtr module, uint thread);
    [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] static extern int GetMessage(out MSG msg, IntPtr hwnd, uint min, uint max);
    [DllImport("user32.dll")] static extern bool TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref MSG msg);
    [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LASTINPUTINFO info);
    [DllImport("xinput1_4.dll")] static extern uint XInputGetState(uint index, out XINPUT_STATE state);
    [DllImport("kernel32.dll")] static extern uint WTSGetActiveConsoleSessionId();
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll", SetLastError = true, EntryPoint = "GetUserObjectInformationW")] static extern bool GetUserObjectInformation(IntPtr obj, int index, byte[] info, uint length, out uint needed);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desk);
    [DllImport("shell32.dll")] static extern int SHQueryUserNotificationState(out int state);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool Process32FirstW(IntPtr snap, ref PROCESSENTRY32W entry);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool Process32NextW(IntPtr snap, ref PROCESSENTRY32W entry);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] static extern bool GetProcessTimes(IntPtr h, out long created, out long exited, out long kernel, out long user);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool QueryFullProcessImageNameW(IntPtr h, uint flags, StringBuilder path, ref uint size);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr h, uint code);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
}
}
