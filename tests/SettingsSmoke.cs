using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using System.Drawing;
using Microsoft.Win32;

public static class SettingsPreview {
    [DllImport("advapi32.dll")] static extern int RegOverridePredefKey(IntPtr key,IntPtr replacement);
    [DllImport("advapi32.dll",CharSet=CharSet.Unicode)] static extern int RegDeleteTree(IntPtr key,string name);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr LoadLibrary(string path);
    [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module,string name);
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("gdi32.dll",EntryPoint="GetObjectW")] static extern int GetFontObject(IntPtr font,int size,[Out]byte[] value);
    [DllImport("ole32.dll")] static extern int CoInitialize(IntPtr value);
    [StructLayout(LayoutKind.Sequential)] struct CommonControls { public uint size, classes; }
    [DllImport("comctl32.dll")] static extern bool InitCommonControlsEx(ref CommonControls controls);
    [DllImport("user32.dll")] static extern bool EnumThreadWindows(uint thread,EnumWindow callback,IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent,EnumWindow callback,IntPtr data);
    [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr window,uint message,IntPtr wParam,IntPtr lParam);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr window,int id);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr wParam,IntPtr lParam);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr ReadListText(IntPtr window,uint message,IntPtr index,StringBuilder text);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool SetWindowText(IntPtr window,string text);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int left,top,right,bottom; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window,out Rect rect);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
    delegate bool EnumWindow(IntPtr window,IntPtr data);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void WaitInit();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void Edit([MarshalAs(UnmanagedType.I1)]bool modal,int tab);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void Initialize([MarshalAs(UnmanagedType.I1)]bool noHook);
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct TreeItem {
        public uint mask; public IntPtr item; public uint state,stateMask; public IntPtr text; public int textMax,image,selectedImage,children; public IntPtr data;
    }
    [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr ReadTreeItem(IntPtr window,uint message,IntPtr wParam,ref TreeItem item);

    static int FontHeight(IntPtr window) {
        var value=new byte[92];
        if(GetFontObject(SendMessage(window,0x31,IntPtr.Zero,IntPtr.Zero),value.Length,value)==0) throw new Exception("Cannot inspect settings font");
        return Math.Abs(BitConverter.ToInt32(value,0));
    }

    static void CheckDisplayScale(IntPtr dialog,IntPtr nav) {
        uint dpi=GetDpiForWindow(dialog); int sidebarFont=FontHeight(nav);
        var failures=new StringBuilder();
        if(sidebarFont>15*dpi/96) failures.Append("Sidebar font "+sidebarFont+"px exceeds window scale "+dpi+" DPI. ");
        Rect navigation; GetWindowRect(nav,out navigation);
        if(navigation.right-navigation.left>220*dpi/96) failures.Append("Sidebar width uses a different scale from its window. ");
        IntPtr style=IntPtr.Zero;
        EnumChildWindows(dialog,delegate(IntPtr w,IntPtr p){if(GetDlgItem(w,1071)!=IntPtr.Zero){style=w;return false;}return true;},IntPtr.Zero);
        if(style!=IntPtr.Zero) {
            Rect preview,replace; GetWindowRect(GetDlgItem(style,1067),out preview); GetWindowRect(GetDlgItem(style,1077),out replace);
            if(preview.bottom>replace.top) failures.Append("Menu preview overlaps the Replace Start button controls. ");
        }
        if(failures.Length>0) throw new Exception(failures.ToString());
    }

    static void CheckEditPersistence(Edit edit,RegistryKey fixture) {
        for(int cycle=0;cycle<3;cycle++) {
            int stage=0; IntPtr dialog=IntPtr.Zero; Exception failure=null;
            var timer=new Timer(); timer.Interval=250;
            timer.Tick+=delegate {
                try {
                    if(stage==3) {
                        IntPtr prompt=IntPtr.Zero;
                        EnumThreadWindows(GetCurrentThreadId(),delegate(IntPtr w,IntPtr p){if(w!=dialog && GetDlgItem(w,7)!=IntPtr.Zero){prompt=w;return false;}return true;},IntPtr.Zero);
                        if(prompt!=IntPtr.Zero) { timer.Stop(); SendMessage(prompt,0x111,new IntPtr(7),IntPtr.Zero); }
                        return;
                    }
                    EnumThreadWindows(GetCurrentThreadId(),delegate(IntPtr w,IntPtr p){if(GetDlgItem(w,30011)!=IntPtr.Zero){dialog=w;return false;}return true;},IntPtr.Zero);
                    if(dialog==IntPtr.Zero) return;
                    IntPtr all=GetDlgItem(dialog,1031);
                    if(SendMessage(all,0xF0,IntPtr.Zero,IntPtr.Zero)==IntPtr.Zero) SendMessage(all,0xF5,IntPtr.Zero,IntPtr.Zero);
                    IntPtr nav=GetDlgItem(dialog,30011);
                    int count=SendMessage(nav,0x18B,IntPtr.Zero,IntPtr.Zero).ToInt32(), taskbar=-1;
                    for(int i=0;i<count;i++) { var name=new StringBuilder(256); ReadListText(nav,0x189,new IntPtr(i),name); if(name.ToString()=="Taskbar") taskbar=i; }
                    if(taskbar<0) throw new Exception("Taskbar category missing during edit test");
                    SendMessage(nav,0x186,new IntPtr(taskbar),IntPtr.Zero);
                    SendMessage(dialog,0x111,new IntPtr(30011|(1<<16)),nav);
                    IntPtr tree=IntPtr.Zero;
                    EnumChildWindows(dialog,delegate(IntPtr w,IntPtr p){if(GetDlgItem(w,236)!=IntPtr.Zero && IsWindowVisible(GetDlgItem(w,236))){tree=GetDlgItem(w,236);return false;}return true;},IntPtr.Zero);
                    if(tree==IntPtr.Zero) throw new Exception("Taskbar settings editor missing");
                    IntPtr first=SendMessage(tree,0x110A,IntPtr.Zero,IntPtr.Zero);
                    var item=new TreeItem { mask=2,item=first }; ReadTreeItem(tree,0x113E,IntPtr.Zero,ref item); int before=item.image;
                    SendMessage(tree,0x110B,new IntPtr(9),first);
                    SendMessage(tree,0x100,new IntPtr(32),IntPtr.Zero);
                    ReadTreeItem(tree,0x113E,IntPtr.Zero,ref item);
                    if(item.image==before) throw new Exception("Space did not toggle the boolean setting");
                    if(cycle==1) { stage=3; PostMessage(dialog,0x111,new IntPtr(2),IntPtr.Zero); }
                    else { timer.Stop(); SendMessage(dialog,0x111,new IntPtr(1),IntPtr.Zero); }
                } catch(Exception ex) { failure=ex; timer.Stop(); if(dialog!=IntPtr.Zero) SendMessage(dialog,0x111,new IntPtr(1),IntPtr.Zero); }
            };
            timer.Start(); edit(true,0); timer.Stop(); timer.Dispose();
            if(failure!=null) throw failure;
            using(var settings=fixture.OpenSubKey(@"Software\OpenShell\StartMenu\Settings")) {
                int expected=cycle==2?1:0;
                if(settings==null || Convert.ToInt32(settings.GetValue("TaskbarRecycleBin",-1))!=expected) throw new Exception("Save/Cancel persistence failed in cycle "+cycle);
            }
        }
    }

    [STAThread] public static void Main(string[] args) {
        string log=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"settings-smoke.log");
        RegistryKey realUser=null, fixture=null; string fixtureName=null;
        try {
            File.WriteAllText(log,"Starting preview.\r\n");
            bool unaware=Array.IndexOf(args,"--unaware")>=0;
            if(!unaware) SetProcessDPIAware();
            string suffix=unaware?"-unaware":"";
            CoInitialize(IntPtr.Zero); Application.EnableVisualStyles();
            if(Array.IndexOf(args,"--thread-unaware")>=0) { SetThreadDpiAwarenessContext(new IntPtr(-1)); suffix="-thread-unaware"; }
            if(Array.IndexOf(args,"--gdi-scaled")>=0) { SetThreadDpiAwarenessContext(new IntPtr(-5)); suffix="-gdi-scaled"; }
            if(args[0]=="--inspect-window") {
                IntPtr window=new IntPtr(long.Parse(args[1])), nav=GetDlgItem(window,30011);
                File.WriteAllText(log,"Window DPI="+GetDpiForWindow(window)+"; sidebar font="+FontHeight(nav)+"px.\r\n");
                CheckDisplayScale(window,nav);
                File.AppendAllText(log,"PASS: installed window scale.\r\n"); return;
            }
            var controls=new CommonControls {size=8,classes=0xFFFF}; InitCommonControlsEx(ref controls);
            bool behavior=args.Length>1 && args[1]=="--behavior";
            if(behavior) {
                realUser=RegistryKey.OpenBaseKey(RegistryHive.CurrentUser,RegistryView.Default);
                fixtureName=@"Software\OpenShell\IntegrationTests\"+Guid.NewGuid().ToString("N");
                fixture=realUser.CreateSubKey(fixtureName);
                int result=RegOverridePredefKey(new IntPtr(unchecked((int)0x80000001)),fixture.Handle.DangerousGetHandle());
                if(result!=0) throw new Exception("Registry fixture isolation failed: "+result);
            }
            IntPtr module=LoadLibrary(args[0]);
            if(module==IntPtr.Zero) throw new Exception("LoadLibrary failed: "+Marshal.GetLastWin32Error());
            ((WaitInit)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module,"?WaitDllInitThread@@YAXXZ"),typeof(WaitInit)))();
            File.AppendAllText(log,"Native settings initialized.\r\n");
            ((Initialize)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module,"?InitManagers@@YAX_N@Z"),typeof(Initialize)))(true);
            if(behavior) {
                CheckEditPersistence((Edit)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module,"?EditSettings@@YAX_NH@Z"),typeof(Edit)),fixture);
                File.WriteAllText(log,"PASS: keyboard boolean editing, Save persistence, Cancel discard, and reopen persistence in an isolated registry fixture.\r\n");
                return;
            }
            bool smoke=args.Length>1 && args[1]=="--smoke";
            int stage=0, originalCount=0; IntPtr dialog=IntPtr.Zero;
            var timer=new Timer(); timer.Interval=700;
            if(smoke) timer.Tick+=delegate {
                try {
                    if(dialog==IntPtr.Zero) EnumThreadWindows(GetCurrentThreadId(),delegate(IntPtr w,IntPtr p){if(GetDlgItem(w,30011)!=IntPtr.Zero){dialog=w;return false;}return true;},IntPtr.Zero);
                    if(dialog==IntPtr.Zero) { if(++stage>8) throw new Exception("Settings window did not expose sidebar"); return; }
                    IntPtr nav=GetDlgItem(dialog,30011), tabs=GetDlgItem(dialog,250);
                    int count=SendMessage(nav,0x18B,IntPtr.Zero,IntPtr.Zero).ToInt32();
                    if(stage==0) {
                        if(count<3) throw new Exception("Expected existing settings categories");
                        if(IsWindowVisible(tabs)) throw new Exception("Old tab strip is still visible");
                        originalCount=count;
                        Rect bounds; GetWindowRect(dialog,out bounds);
                        var area=Screen.FromHandle(dialog).WorkingArea;
                        if(bounds.bottom-bounds.top>area.Height || bounds.right-bounds.left>area.Width) throw new Exception("Settings window exceeds monitor work area: "+(bounds.right-bounds.left)+"x"+(bounds.bottom-bounds.top)+" versus "+area.Width+"x"+area.Height);
                        using(var bitmap=new Bitmap(bounds.right-bounds.left,bounds.bottom-bounds.top)) {
                            using(var graphics=Graphics.FromImage(bitmap)) { var dc=graphics.GetHdc(); PrintWindow(dialog,dc,2); graphics.ReleaseHdc(dc); }
                            bitmap.Save(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"settings-preview"+suffix+".png"));
                        }
                        CheckDisplayScale(dialog,nav);
                        for(int index=0;index<count;index++) {
                            var name=new StringBuilder(256); ReadListText(nav,0x189,new IntPtr(index),name);
                            SendMessage(nav,0x186,new IntPtr(index),IntPtr.Zero);
                            SendMessage(dialog,0x111,new IntPtr(30011|(1<<16)),nav);
                            if(SendMessage(tabs,0x130B,IntPtr.Zero,IntPtr.Zero).ToInt32()!=index) throw new Exception("Sidebar failed to select existing panel: "+name);
                            if(name.ToString()=="Taskbar") {
                                using(var bitmap=new Bitmap(bounds.right-bounds.left,bounds.bottom-bounds.top)) {
                                    using(var graphics=Graphics.FromImage(bitmap)) { var dc=graphics.GetHdc(); PrintWindow(dialog,dc,2); graphics.ReleaseHdc(dc); }
                                    bitmap.Save(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"taskbar-settings-preview"+suffix+".png"));
                                }
                            }
                        }
                        SetWindowText(GetDlgItem(dialog,1025),"taskbar"); stage=1;
                    } else if(stage==1) {
                        if(count!=1) throw new Exception("Search did not reduce navigation to results");
                        SetWindowText(GetDlgItem(dialog,1025),""); stage=2;
                    } else {
                        if(count!=originalCount) throw new Exception("Clearing search lost settings categories");
                        File.WriteAllText(log,"PASS: "+originalCount+" settings pages, hidden legacy tabs, panel navigation, search, search restoration, monitor bounds, and consistent "+GetDpiForWindow(dialog)+" DPI layout.\r\n");
                        timer.Stop(); SendMessage(dialog,0x111,new IntPtr(2),IntPtr.Zero);
                    }
                } catch(Exception ex) { File.WriteAllText(log,"FAIL: "+ex); Environment.ExitCode=1; timer.Stop(); if(dialog!=IntPtr.Zero) SendMessage(dialog,0x111,new IntPtr(2),IntPtr.Zero); }
            };
            if(smoke) timer.Start();
            ((Edit)Marshal.GetDelegateForFunctionPointer(GetProcAddress(module,"?EditSettings@@YAX_NH@Z"),typeof(Edit)))(true,0);
            if(stage==0) File.AppendAllText(log,"Settings returned before smoke test. Win32 error="+Marshal.GetLastWin32Error());
        } catch(Exception ex) { File.WriteAllText(log,"FAIL: "+ex); Environment.ExitCode=1; }
        finally {
            if(fixture!=null) { RegOverridePredefKey(new IntPtr(unchecked((int)0x80000001)),IntPtr.Zero); fixture.Dispose(); }
            if(realUser!=null) {
                if(fixtureName!=null) {
                    int result=RegDeleteTree(realUser.Handle.DangerousGetHandle(),fixtureName);
                    if(result!=0 && result!=2) { File.AppendAllText(log,"Fixture cleanup returned "+result+".\r\n"); Environment.ExitCode=1; }
                }
                realUser.Dispose();
            }
        }
    }
}
