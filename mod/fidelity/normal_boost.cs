// mapbase-fidelity: steeper normal maps, for deploy.ps1 (loaded with Add-Type).
//
// Source SDK 2013's world and model shaders have no setting for how strong a
// bump map is. deploy.ps1 makes copies of the game's normal maps in the mod
// with the slopes made steeper: each normal's x and y times the strength, z
// kept, then normalised again - the relief shows more in every light. Every
// mip level and frame is changed; the alpha channel (specular / Phong mask)
// and the low-res thumbnail stay as they are.
//
// Formats: the plain 8 bit ones (RGBA8888, ABGR8888, RGB888, BGR888,
// ARGB8888, BGRA8888, BGRX8888) and DXT1/3/5 (each 4x4 block decoded,
// changed and encoded again; DXT1 blocks with transparent texels are kept).
// Not: cube maps, volume textures, self-shadowed bump maps (SSBump: they hold
// light directions, not normals), other formats. Every offset is checked: a
// broken file is reported, not read past its end.
//
// C# 5 (Windows PowerShell 5.1 compiles with the .NET Framework compiler).

using System;

public static class FidelityNormalBoost
{
	const int RGBA8888 = 0, ABGR8888 = 1, RGB888 = 2, BGR888 = 3, ARGB8888 = 11, BGRA8888 = 12;
	const int DXT1 = 13, DXT3 = 14, DXT5 = 15, BGRX8888 = 16, DXT1_ONEBITALPHA = 20;
	const int FLAG_ENVMAP = 0x4000, FLAG_SSBUMP = 0x08000000;

	static int Int32At( byte[] b, int o ) { return b[ o ] | ( b[ o + 1 ] << 8 ) | ( b[ o + 2 ] << 16 ) | ( b[ o + 3 ] << 24 ); }
	static int Int16At( byte[] b, int o ) { return b[ o ] | ( b[ o + 1 ] << 8 ); }

	// Bytes of one image of w x h texels, or -1 for a format not handled
	static long ImageSize( int w, int h, int format )
	{
		w = Math.Max( w, 1 );
		h = Math.Max( h, 1 );
		long blocks = (long)Math.Max( ( w + 3 ) / 4, 1 ) * Math.Max( ( h + 3 ) / 4, 1 );
		switch ( format )
		{
			case DXT1: case DXT1_ONEBITALPHA: return blocks * 8;
			case DXT3: case DXT5: return blocks * 16;
			case RGBA8888: case ABGR8888: case ARGB8888: case BGRA8888: case BGRX8888: return (long)w * h * 4;
			case RGB888: case BGR888: return (long)w * h * 3;
		}
		return -1;
	}

	// Bytes per texel and where red, green and blue are, for the plain formats
	static bool Layout( int format, out int bytes, out int r, out int g, out int b )
	{
		bytes = 4; r = 0; g = 1; b = 2;
		switch ( format )
		{
			case RGBA8888: return true;
			case ABGR8888: r = 3; g = 2; b = 1; return true;
			case RGB888: bytes = 3; return true;
			case BGR888: bytes = 3; r = 2; g = 1; b = 0; return true;
			case ARGB8888: r = 1; g = 2; b = 3; return true;
			case BGRA8888: case BGRX8888: r = 2; g = 1; b = 0; return true;
		}
		return false;
	}

	// One normal (0..255 per channel) made steeper
	static void Steeper( ref float x, ref float y, ref float z, float strength )
	{
		float nx = ( x / 255f * 2f - 1f ) * strength;
		float ny = ( y / 255f * 2f - 1f ) * strength;
		float nz = Math.Max( z / 255f * 2f - 1f, 0f );
		float length = (float)Math.Sqrt( nx * nx + ny * ny + nz * nz );
		if ( length < 1e-6f )
		{
			nx = 0f; ny = 0f; nz = 1f; length = 1f;
		}
		x = ( nx / length * 0.5f + 0.5f ) * 255f;
		y = ( ny / length * 0.5f + 0.5f ) * 255f;
		z = ( nz / length * 0.5f + 0.5f ) * 255f;
	}

	static byte ToByte( float v ) { return (byte)Math.Max( 0, Math.Min( 255, (int)( v + 0.5f ) ) ); }

	static void Unpack565( int c, out float r, out float g, out float b )
	{
		int r5 = ( c >> 11 ) & 31, g6 = ( c >> 5 ) & 63, b5 = c & 31;
		r = ( r5 << 3 ) | ( r5 >> 2 );
		g = ( g6 << 2 ) | ( g6 >> 4 );
		b = ( b5 << 3 ) | ( b5 >> 2 );
	}

	static int Pack565( float r, float g, float b )
	{
		int r5 = Math.Max( 0, Math.Min( 31, (int)( r * 31f / 255f + 0.5f ) ) );
		int g6 = Math.Max( 0, Math.Min( 63, (int)( g * 63f / 255f + 0.5f ) ) );
		int b5 = Math.Max( 0, Math.Min( 31, (int)( b * 31f / 255f + 0.5f ) ) );
		return ( r5 << 11 ) | ( g6 << 5 ) | b5;
	}

	// The colour part of a DXT block at o (8 bytes); bFourColour: DXT3/5 always use four colours
	static void BoostColourBlock( byte[] data, int o, float strength, bool bFourColour )
	{
		int c0 = Int16At( data, o ), c1 = Int16At( data, o + 2 );
		if ( !bFourColour && c0 < c1 )
			return;	// DXT1 with transparent texels: kept
		if ( !bFourColour && c0 == c1 )
		{
			// A block of one colour (compressors write these for even areas):
			// three colours and transparent, so it is only kept if a texel is
			// transparent - a tilted even area was left as it was among
			// steeper blocks around it (seams along the blocks)
			uint flat = (uint)Int32At( data, o + 4 );
			for ( int i = 0; i < 16; i++ )
			{
				if ( ( ( flat >> ( 2 * i ) ) & 3 ) == 3 )
					return;
			}
		}
		float[] pr = new float[ 4 ], pg = new float[ 4 ], pb = new float[ 4 ];
		Unpack565( c0, out pr[ 0 ], out pg[ 0 ], out pb[ 0 ] );
		Unpack565( c1, out pr[ 1 ], out pg[ 1 ], out pb[ 1 ] );
		pr[ 2 ] = ( 2 * pr[ 0 ] + pr[ 1 ] ) / 3; pg[ 2 ] = ( 2 * pg[ 0 ] + pg[ 1 ] ) / 3; pb[ 2 ] = ( 2 * pb[ 0 ] + pb[ 1 ] ) / 3;
		pr[ 3 ] = ( pr[ 0 ] + 2 * pr[ 1 ] ) / 3; pg[ 3 ] = ( pg[ 0 ] + 2 * pg[ 1 ] ) / 3; pb[ 3 ] = ( pb[ 0 ] + 2 * pb[ 1 ] ) / 3;

		// The 16 texels, made steeper
		uint indices = (uint)Int32At( data, o + 4 );
		float[] tr = new float[ 16 ], tg = new float[ 16 ], tb = new float[ 16 ];
		for ( int i = 0; i < 16; i++ )
		{
			int k = (int)( ( indices >> ( 2 * i ) ) & 3 );
			tr[ i ] = pr[ k ]; tg[ i ] = pg[ k ]; tb[ i ] = pb[ k ];
			Steeper( ref tr[ i ], ref tg[ i ], ref tb[ i ], strength );
		}

		// New end colours: the two texels furthest apart
		int a = 0, b = 0;
		float best = -1f;
		for ( int i = 0; i < 16; i++ )
		{
			for ( int j = i + 1; j < 16; j++ )
			{
				float dr = tr[ i ] - tr[ j ], dg = tg[ i ] - tg[ j ], db = tb[ i ] - tb[ j ];
				float d = dr * dr + dg * dg + db * db;
				if ( d > best ) { best = d; a = i; b = j; }
			}
		}
		int n0 = Pack565( tr[ a ], tg[ a ], tb[ a ] ), n1 = Pack565( tr[ b ], tg[ b ], tb[ b ] );
		if ( n0 < n1 ) { int t = n0; n0 = n1; n1 = t; }
		if ( n0 == n1 )
		{
			// One colour: all texels index 0 (c0 > c1 is not possible; a DXT1
			// block with c0 == c1 shows colour 0 for index 0 all the same)
			data[ o ] = (byte)n0; data[ o + 1 ] = (byte)( n0 >> 8 );
			data[ o + 2 ] = (byte)n1; data[ o + 3 ] = (byte)( n1 >> 8 );
			data[ o + 4 ] = data[ o + 5 ] = data[ o + 6 ] = data[ o + 7 ] = 0;
			return;
		}
		Unpack565( n0, out pr[ 0 ], out pg[ 0 ], out pb[ 0 ] );
		Unpack565( n1, out pr[ 1 ], out pg[ 1 ], out pb[ 1 ] );
		pr[ 2 ] = ( 2 * pr[ 0 ] + pr[ 1 ] ) / 3; pg[ 2 ] = ( 2 * pg[ 0 ] + pg[ 1 ] ) / 3; pb[ 2 ] = ( 2 * pb[ 0 ] + pb[ 1 ] ) / 3;
		pr[ 3 ] = ( pr[ 0 ] + 2 * pr[ 1 ] ) / 3; pg[ 3 ] = ( pg[ 0 ] + 2 * pg[ 1 ] ) / 3; pb[ 3 ] = ( pb[ 0 ] + 2 * pb[ 1 ] ) / 3;
		uint newIndices = 0;
		for ( int i = 0; i < 16; i++ )
		{
			int nearest = 0;
			float nearestD = float.MaxValue;
			for ( int k = 0; k < 4; k++ )
			{
				float dr = tr[ i ] - pr[ k ], dg = tg[ i ] - pg[ k ], db = tb[ i ] - pb[ k ];
				float d = dr * dr + dg * dg + db * db;
				if ( d < nearestD ) { nearestD = d; nearest = k; }
			}
			newIndices |= (uint)nearest << ( 2 * i );
		}
		data[ o ] = (byte)n0; data[ o + 1 ] = (byte)( n0 >> 8 );
		data[ o + 2 ] = (byte)n1; data[ o + 3 ] = (byte)( n1 >> 8 );
		data[ o + 4 ] = (byte)newIndices; data[ o + 5 ] = (byte)( newIndices >> 8 );
		data[ o + 6 ] = (byte)( newIndices >> 16 ); data[ o + 7 ] = (byte)( newIndices >> 24 );
	}

	static void BoostImage( byte[] data, long offset, int w, int h, int format, float strength )
	{
		int o = (int)offset;
		if ( format == DXT1 || format == DXT1_ONEBITALPHA || format == DXT3 || format == DXT5 )
		{
			int blockSize = ( format == DXT3 || format == DXT5 ) ? 16 : 8;
			int colourAt = blockSize - 8;	// DXT3/5: alpha block first, kept
			long blocks = (long)Math.Max( ( w + 3 ) / 4, 1 ) * Math.Max( ( h + 3 ) / 4, 1 );
			for ( long i = 0; i < blocks; i++ )
				BoostColourBlock( data, o + (int)( i * blockSize ) + colourAt, strength, blockSize == 16 );
			return;
		}
		int bytes, r, g, b;
		if ( !Layout( format, out bytes, out r, out g, out b ) )
			return;
		long texels = (long)Math.Max( w, 1 ) * Math.Max( h, 1 );
		for ( long i = 0; i < texels; i++ )
		{
			int p = o + (int)( i * bytes );
			float x = data[ p + r ], y = data[ p + g ], z = data[ p + b ];
			Steeper( ref x, ref y, ref z, strength );
			data[ p + r ] = ToByte( x ); data[ p + g ] = ToByte( y ); data[ p + b ] = ToByte( z );
		}
	}

	// A copy of the .vtf with steeper normals, or null with the reason
	public static byte[] Boost( byte[] vtf, float strength, out string reason )
	{
		reason = null;
		if ( vtf == null || vtf.Length < 80 || vtf[ 0 ] != 'V' || vtf[ 1 ] != 'T' || vtf[ 2 ] != 'F' || Int32At( vtf, 4 ) != 7 )
		{
			reason = "not a .vtf";
			return null;
		}
		int minor = Int32At( vtf, 8 );
		int headerSize = Int32At( vtf, 12 );
		int width = Int16At( vtf, 16 ), height = Int16At( vtf, 18 );
		int flags = Int32At( vtf, 20 );
		int frames = Math.Max( Int16At( vtf, 24 ), 1 );
		int format = Int32At( vtf, 52 );
		int mips = vtf[ 56 ];
		int lowFormat = Int32At( vtf, 57 );
		int lowWidth = vtf[ 61 ], lowHeight = vtf[ 62 ];
		int depth = minor >= 2 ? Math.Max( Int16At( vtf, 63 ), 1 ) : 1;
		if ( minor < 0 || minor > 5 || headerSize < 64 || headerSize > vtf.Length )
		{
			reason = "unknown .vtf version";
			return null;
		}
		if ( ( flags & FLAG_SSBUMP ) != 0 )
		{
			reason = "self-shadowed bump map";
			return null;
		}
		if ( ( flags & FLAG_ENVMAP ) != 0 || depth != 1 )
		{
			reason = "cube map or volume";
			return null;
		}
		if ( width < 1 || height < 1 || mips < 1 || mips > 17 || ImageSize( width, height, format ) < 0 )
		{
			reason = "format " + format + " not handled";
			return null;
		}

		// Where the mip levels start: resource 0x30 (7.3+), else after the header and the thumbnail
		long start = -1;
		if ( minor >= 3 )
		{
			int resources = Int32At( vtf, 68 );
			for ( int i = 0; i < resources && 80 + i * 8 + 8 <= headerSize; i++ )
			{
				int at = 80 + i * 8;
				if ( vtf[ at ] == 0x30 && vtf[ at + 1 ] == 0 && vtf[ at + 2 ] == 0 )
					start = Int32At( vtf, at + 4 ) & 0x7fffffff;
			}
		}
		else
		{
			long low = ( lowFormat >= 0 && lowWidth > 0 && lowHeight > 0 ) ? ImageSize( lowWidth, lowHeight, lowFormat ) : 0;
			start = headerSize + Math.Max( low, 0 );
		}
		if ( start < headerSize )
		{
			reason = "no image data";
			return null;
		}

		// Smallest level first, each with all frames
		long total = 0;
		for ( int level = mips - 1; level >= 0; level-- )
			total += frames * ImageSize( width >> level, height >> level, format );
		if ( start + total > vtf.Length )
		{
			reason = "cut off";
			return null;
		}
		byte[] result = (byte[])vtf.Clone();
		long offset = start;
		for ( int level = mips - 1; level >= 0; level-- )
		{
			int w = Math.Max( width >> level, 1 ), h = Math.Max( height >> level, 1 );
			long size = ImageSize( w, h, format );
			for ( int frame = 0; frame < frames; frame++ )
			{
				BoostImage( result, offset, w, h, format, strength );
				offset += size;
			}
		}
		return result;
	}
}
