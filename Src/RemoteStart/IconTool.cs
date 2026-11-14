// Saves the shell's icon for a file, shortcut or app ("shell:AppsFolder\<AppUserModelID>") as a PNG-based .ico.
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

public static class RemoteStartIconTool
{
	[ComImport, Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
	interface IShellItemImageFactory
	{
		[PreserveSig] int GetImage(Size size, int flags, out IntPtr bitmap);
	}

	[DllImport("shell32.dll", CharSet=CharSet.Unicode, PreserveSig=false)]
	static extern void SHCreateItemFromParsingName(string path, IntPtr bindContext, [MarshalAs(UnmanagedType.LPStruct)] Guid riid, [MarshalAs(UnmanagedType.Interface)] out IShellItemImageFactory factory);
	[DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr handle);
	[DllImport("gdi32.dll")] static extern int GetDIBits(IntPtr dc, IntPtr bitmap, uint start, uint lines, byte[] bits, ref BITMAPINFOHEADER info, uint usage);
	[DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr window);
	[DllImport("user32.dll")] static extern int ReleaseDC(IntPtr window, IntPtr dc);

	[StructLayout(LayoutKind.Sequential)]
	struct BITMAPINFOHEADER
	{
		public int biSize, biWidth, biHeight;
		public short biPlanes, biBitCount;
		public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
		public int extra1, extra2, extra3; // room for color masks
	}

	public static byte[] IcoBytes(string parseName, int size)
	{
		IShellItemImageFactory factory;
		try { SHCreateItemFromParsingName(parseName, IntPtr.Zero, typeof(IShellItemImageFactory).GUID, out factory); }
		catch { return null; }
		IntPtr handle;
		if (factory.GetImage(new Size(size, size), 4 /*SIIGBF_ICONONLY*/, out handle) < 0 || handle == IntPtr.Zero) return null;
		try
		{
			BITMAPINFOHEADER info = new BITMAPINFOHEADER();
			info.biSize = 40; info.biWidth = size; info.biHeight = -size; info.biPlanes = 1; info.biBitCount = 32;
			byte[] bits = new byte[size * size * 4];
			IntPtr dc = GetDC(IntPtr.Zero);
			int lines = GetDIBits(dc, handle, 0, (uint)size, bits, ref info, 0);
			ReleaseDC(IntPtr.Zero, dc);
			if (lines != size) return null;
			using (Bitmap bitmap = new Bitmap(size, size, PixelFormat.Format32bppPArgb))
			{
				BitmapData data = bitmap.LockBits(new Rectangle(0, 0, size, size), ImageLockMode.WriteOnly, PixelFormat.Format32bppPArgb);
				Marshal.Copy(bits, 0, data.Scan0, bits.Length);
				bitmap.UnlockBits(data);
				using (MemoryStream png = new MemoryStream())
				using (MemoryStream ico = new MemoryStream())
				using (BinaryWriter writer = new BinaryWriter(ico))
				{
					bitmap.Save(png, ImageFormat.Png);
					writer.Write((short)0); writer.Write((short)1); writer.Write((short)1);
					writer.Write((byte)size); writer.Write((byte)size); writer.Write((byte)0); writer.Write((byte)0);
					writer.Write((short)1); writer.Write((short)32); writer.Write((int)png.Length); writer.Write(22);
					writer.Write(png.ToArray());
					writer.Flush();
					return ico.ToArray();
				}
			}
		}
		finally
		{
			DeleteObject(handle);
		}
	}
}
