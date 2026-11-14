// RemoteStart: published as a RemoteApp on the remote PC so the red Start button on the
// local taskbar can open that PC's Start menu items.
// It only opens Start Menu shortcuts and installed apps; it is not a general command runner.
//   RemoteStart.exe "common:<path under All Users Start Menu\Programs>"
//   RemoteStart.exe "user:<path under the user's Start Menu\Programs>"
//   RemoteStart.exe "app:<AppUserModelID of an installed app>"
//   RemoteStart.exe "disconnect:"  disconnects the RemoteApp session it runs in
// Build: %WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:winexe /out:RemoteStart.exe RemoteStart.cs

using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;

[ComImport, Guid("2e941141-7f97-4756-ba1d-9decde894a3d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IApplicationActivationManager
{
	[PreserveSig] int ActivateApplication([MarshalAs(UnmanagedType.LPWStr)] string appUserModelId, [MarshalAs(UnmanagedType.LPWStr)] string arguments, int options, out uint processId);
}

[ComImport, Guid("45BA127D-10A8-46EA-8AB7-56EA9078943C")]
class ApplicationActivationManager {}

static class RemoteStart
{
	[DllImport("wtsapi32.dll",SetLastError=true)]
	static extern bool WTSDisconnectSession( IntPtr server, int sessionId, bool wait );

	[STAThread]
	static int Main( string[] args )
	{
		if (args.Length!=1) return 2;
		string item=args[0];
		try
		{
			if (item=="disconnect:")
			{
				// ends this RemoteApp connection; the session and its apps keep running on this PC
				return WTSDisconnectSession(IntPtr.Zero,-1,false)?0:8;
			}
			if (item.StartsWith("app:",StringComparison.Ordinal))
			{
				string id=item.Substring(4);
				if (id.IndexOf('!')<1 || id.IndexOfAny(new[]{'\\','/',':','"',' '})>=0) return 3;
				uint pid;
				int hr=((IApplicationActivationManager)new ApplicationActivationManager()).ActivateApplication(id,null,0,out pid);
				return hr<0?7:0;
			}
			bool common=item.StartsWith("common:",StringComparison.Ordinal);
			if (!common && !item.StartsWith("user:",StringComparison.Ordinal)) return 3;
			string root=Path.Combine(Environment.GetFolderPath(common?Environment.SpecialFolder.CommonStartMenu:Environment.SpecialFolder.StartMenu),"Programs");
			string path=Path.GetFullPath(Path.Combine(root,item.Substring(common?7:5)));
			string ext=Path.GetExtension(path).ToLowerInvariant();
			if (!path.StartsWith(root.TrimEnd('\\')+"\\",StringComparison.OrdinalIgnoreCase) || !File.Exists(path)) return 4;
			if (ext!=".lnk" && ext!=".url" && ext!=".appref-ms") return 5;
			Process.Start(new ProcessStartInfo(path){UseShellExecute=true});
			return 0;
		}
		catch
		{
			return 1;
		}
	}
}
