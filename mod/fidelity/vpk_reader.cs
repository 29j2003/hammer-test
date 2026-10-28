// mapbase-fidelity: reading the game's .vpk archives, for deploy.ps1 and
// sound_tool.ps1 (loaded with Add-Type by fidelity_common.ps1).
//
// A .vpk (version 1 or 2) is a _dir.vpk with a directory tree - extension,
// folder, name, and where each file's data is: in the _dir.vpk itself or in
// <name>_NNN.vpk - plus a little "preload" data in the tree. PowerShell read
// the tree string by string and looked at every entry with a script block:
// with the game's tens of thousands of files and a dozen passes over them that
// was most of deploy.bat's time. Here the tree is read once into arrays,
// passes pick their entries by extension and folder before PowerShell sees
// any, and file data is copied in one go.
//
// A damaged archive (cut short by an update, say) gives the entries before
// the damage; entries whose data is not where the tree says, or larger than
// MaxFile, read as null.
//
// C# 5 (Windows PowerShell 5.1 compiles with the .NET Framework compiler).

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

public sealed class FidelityVpk
{
	public const long MaxFile = 64L * 1024 * 1024;

	public readonly string Path;
	public byte[] Bytes;		// the whole _dir.vpk, or null (not a .vpk / unreadable)
	public long DataStart;		// where data stored in the _dir.vpk itself begins
	public int Count;
	public string[] Ext, Dir, Name, Key;	// Key: "dir/name.ext" as the game names the file
	public int[] Archive, PreloadStart, Preload;
	public uint[] Offset, Length;
	public string Error;		// why the archive could not be read, or null

	// Latin-1 maps every byte 1:1 (as fidelity_common.ps1's $Encoding)
	static readonly Encoding Latin1 = Encoding.GetEncoding( 28591 );

	public FidelityVpk( string dirFile )
	{
		Path = dirFile;
		Ext = Dir = Name = Key = new string[ 0 ];
		Archive = PreloadStart = Preload = new int[ 0 ];
		Offset = Length = new uint[ 0 ];
		byte[] bytes;
		try { bytes = File.ReadAllBytes( dirFile ); }
		catch ( Exception e ) { Error = e.Message; return; }
		if ( bytes.Length < 12 || BitConverter.ToUInt32( bytes, 0 ) != 0x55aa1234 )
			return;
		uint version = BitConverter.ToUInt32( bytes, 4 );
		int headerSize = version == 2 ? 28 : 12;
		Bytes = bytes;
		DataStart = (long)headerSize + BitConverter.ToUInt32( bytes, 8 );

		var ext = new List<string>(); var dir = new List<string>(); var name = new List<string>(); var key = new List<string>();
		var archive = new List<int>(); var preloadStart = new List<int>(); var preload = new List<int>();
		var offset = new List<uint>(); var length = new List<uint>();
		int pos = headerSize;
		string e1, d1, n1;
		while ( ReadString( bytes, ref pos, out e1 ) && e1.Length > 0 )
		{
			while ( true )
			{
				if ( !ReadString( bytes, ref pos, out d1 ) ) goto done;
				if ( d1.Length == 0 ) break;
				while ( true )
				{
					if ( !ReadString( bytes, ref pos, out n1 ) ) goto done;
					if ( n1.Length == 0 ) break;
					if ( pos + 18 > bytes.Length ) goto done;
					int pre = BitConverter.ToUInt16( bytes, pos + 4 );
					int start = pos + 18;
					pos += 18 + pre;
					if ( pos > bytes.Length ) goto done;
					ext.Add( e1 ); dir.Add( d1 ); name.Add( n1 ); key.Add( d1 + "/" + n1 + "." + e1 );
					archive.Add( BitConverter.ToUInt16( bytes, start - 12 ) );
					offset.Add( BitConverter.ToUInt32( bytes, start - 10 ) );
					length.Add( BitConverter.ToUInt32( bytes, start - 6 ) );
					preloadStart.Add( start );
					preload.Add( pre );
				}
			}
		}
	done:
		Count = ext.Count;
		Ext = ext.ToArray(); Dir = dir.ToArray(); Name = name.ToArray(); Key = key.ToArray();
		Archive = archive.ToArray(); PreloadStart = preloadStart.ToArray(); Preload = preload.ToArray();
		Offset = offset.ToArray(); Length = length.ToArray();
	}

	// The next zero-terminated string at pos; false at the end of the file
	static bool ReadString( byte[] bytes, ref int pos, out string s )
	{
		s = null;
		if ( pos >= bytes.Length ) return false;
		int z = Array.IndexOf( bytes, (byte)0, pos );
		if ( z < 0 ) return false;
		s = Latin1.GetString( bytes, pos, z - pos );
		pos = z + 1;
		return true;
	}

	// The entries with this extension ("" = any) in one of these folders and
	// below (none = anywhere; "materials/" also matches "materials" itself), in tree order
	public int[] Find( string ext, string[] prefixes )
	{
		var found = new List<int>();
		for ( int i = 0; i < Count; i++ )
		{
			if ( ext.Length > 0 && !string.Equals( Ext[ i ], ext, StringComparison.Ordinal ) )
				continue;
			if ( prefixes != null && prefixes.Length > 0 )
			{
				bool match = false;
				string d = Dir[ i ] + "/";
				foreach ( string p in prefixes )
				{
					if ( d.StartsWith( p.EndsWith( "/" ) ? p : p + "/", StringComparison.Ordinal ) )
					{
						match = true;
						break;
					}
				}
				if ( !match )
					continue;
			}
			found.Add( i );
		}
		return found.ToArray();
	}

	// The data of entry i (at most limit bytes when limit > 0), or null when it
	// is not where the tree says or too large. Open archive files are kept in
	// streams (the caller closes them with Close).
	public byte[] Read( int i, int limit, Dictionary<int, FileStream> streams )
	{
		long total = (long)Preload[ i ] + Length[ i ];
		if ( limit > 0 && total > limit ) total = limit;
		if ( total > MaxFile ) return null;
		byte[] data = new byte[ total ];
		int fromPreload = (int)Math.Min( (long)Preload[ i ], total );
		Buffer.BlockCopy( Bytes, PreloadStart[ i ], data, 0, fromPreload );
		long rest = total - fromPreload;
		if ( rest <= 0 ) return data;
		if ( Archive[ i ] == 0x7fff )
		{
			long from = DataStart + Offset[ i ];
			if ( from + rest > Bytes.Length ) return null;
			Buffer.BlockCopy( Bytes, (int)from, data, fromPreload, (int)rest );
			return data;
		}
		FileStream stream;
		if ( !streams.TryGetValue( Archive[ i ], out stream ) )
		{
			string archive = Path.Substring( 0, Path.Length - "dir.vpk".Length ) + Archive[ i ].ToString( "000" ) + ".vpk";
			if ( !File.Exists( archive ) ) return null;
			stream = File.OpenRead( archive );
			streams[ Archive[ i ] ] = stream;
		}
		if ( Offset[ i ] + rest > stream.Length ) return null;
		stream.Seek( Offset[ i ], SeekOrigin.Begin );
		int read = 0;
		while ( read < rest )
		{
			int n = stream.Read( data, fromPreload + read, (int)( rest - read ) );
			if ( n <= 0 ) return null;
			read += n;
		}
		return data;
	}

	public static void Close( Dictionary<int, FileStream> streams )
	{
		foreach ( FileStream stream in streams.Values )
			stream.Dispose();
		streams.Clear();
	}
}
