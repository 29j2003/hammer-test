// mapbase-fidelity: height maps from normal maps, for deploy.ps1 (loaded with Add-Type).
//
// Fidelity_Parallax (parallax occlusion mapping on walls and floors) needs to
// know how high each point of a texture is. Half-Life 2 has no height maps,
// but every bump-mapped material has a normal map: its slopes are the
// height's gradient, and integrating them gives the height back.
//
//   1. The normal map is read (the largest mip level up to 512 texels a side;
//      8 bit formats and DXT1/3/5). A self-shadowed bump map ($ssbump: the
//      weights of the three lightmap directions) is turned into normals.
//   2. Slopes: dh/dx = -nx/nz, dh/dy = -ny/nz, with green along the texture's
//      v (down in the image), as Source's tools make them. A map made the
//      other way round shows itself: its slopes do not fit together (curl);
//      then green is turned round.
//   3. Integrated in the frequency domain (Frankot-Chellappa: textures tile,
//      which is what the FFT assumes anyway). Very slow waves (less than 3 a
//      texture) are taken out: a normal map's small errors add up to large
//      bowls, which the parallax would show as a tilted wall.
//   4. Scaled to 0..1 (1 % and 99 % of the heights), written as an I8 .vtf
//      with all mip levels.
//
// Walls without a bump map (most of a map's bricks, stones and tiles) get
// their height from the base texture instead (MakeFromColor, ColorHeight):
// its brightness, band-passed, the joints down; where it finds a network of
// joints, each stone is shaped from them (flat top, rounded edge). Bricks,
// tiles and blocks (MakeFromCourses): their courses are found as repeating
// thin lines, each brick a clean rectangle (CourseRelief).
//
// C# 5 (Windows PowerShell 5.1 compiles with the .NET Framework compiler).

using System;

public static class FidelityHeightMap
{
	const int RGBA8888 = 0, ABGR8888 = 1, RGB888 = 2, BGR888 = 3, ARGB8888 = 11, BGRA8888 = 12;
	const int DXT1 = 13, DXT3 = 14, DXT5 = 15, BGRX8888 = 16, DXT1_ONEBITALPHA = 20;
	const int FLAG_ENVMAP = 0x4000, FLAG_SSBUMP = 0x08000000;
	const int MAX_SIZE = 512;

	static int Int32At( byte[] b, int o ) { return b[ o ] | ( b[ o + 1 ] << 8 ) | ( b[ o + 2 ] << 16 ) | ( b[ o + 3 ] << 24 ); }
	static int Int16At( byte[] b, int o ) { return b[ o ] | ( b[ o + 1 ] << 8 ); }

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

	static void Unpack565( int c, out float r, out float g, out float b )
	{
		int r5 = ( c >> 11 ) & 31, g6 = ( c >> 5 ) & 63, b5 = c & 31;
		r = ( ( r5 << 3 ) | ( r5 >> 2 ) ) / 255f;
		g = ( ( g6 << 2 ) | ( g6 >> 4 ) ) / 255f;
		b = ( ( b5 << 3 ) | ( b5 >> 2 ) ) / 255f;
	}

	// Decodes one image to r, g, b (0..1) per texel
	static void Decode( byte[] data, int o, int w, int h, int format, float[] r, float[] g, float[] b )
	{
		if ( format == DXT1 || format == DXT1_ONEBITALPHA || format == DXT3 || format == DXT5 )
		{
			int blockSize = ( format == DXT3 || format == DXT5 ) ? 16 : 8;
			int bw = Math.Max( ( w + 3 ) / 4, 1 ), bh = Math.Max( ( h + 3 ) / 4, 1 );
			float[] pr = new float[ 4 ], pg = new float[ 4 ], pb = new float[ 4 ];
			for ( int by = 0; by < bh; by++ )
			{
				for ( int bx = 0; bx < bw; bx++ )
				{
					int at = o + ( by * bw + bx ) * blockSize + blockSize - 8;
					int c0 = Int16At( data, at ), c1 = Int16At( data, at + 2 );
					Unpack565( c0, out pr[ 0 ], out pg[ 0 ], out pb[ 0 ] );
					Unpack565( c1, out pr[ 1 ], out pg[ 1 ], out pb[ 1 ] );
					if ( blockSize == 16 || c0 > c1 )
					{
						pr[ 2 ] = ( 2 * pr[ 0 ] + pr[ 1 ] ) / 3; pg[ 2 ] = ( 2 * pg[ 0 ] + pg[ 1 ] ) / 3; pb[ 2 ] = ( 2 * pb[ 0 ] + pb[ 1 ] ) / 3;
						pr[ 3 ] = ( pr[ 0 ] + 2 * pr[ 1 ] ) / 3; pg[ 3 ] = ( pg[ 0 ] + 2 * pg[ 1 ] ) / 3; pb[ 3 ] = ( pb[ 0 ] + 2 * pb[ 1 ] ) / 3;
					}
					else
					{
						pr[ 2 ] = ( pr[ 0 ] + pr[ 1 ] ) / 2; pg[ 2 ] = ( pg[ 0 ] + pg[ 1 ] ) / 2; pb[ 2 ] = ( pb[ 0 ] + pb[ 1 ] ) / 2;
						pr[ 3 ] = 0.5f; pg[ 3 ] = 0.5f; pb[ 3 ] = 1f;	// transparent: a flat normal
					}
					uint indices = (uint)Int32At( data, at + 4 );
					for ( int i = 0; i < 16; i++ )
					{
						int x = bx * 4 + ( i & 3 ), y = by * 4 + ( i >> 2 );
						if ( x >= w || y >= h )
							continue;
						int k = (int)( ( indices >> ( 2 * i ) ) & 3 );
						r[ y * w + x ] = pr[ k ]; g[ y * w + x ] = pg[ k ]; b[ y * w + x ] = pb[ k ];
					}
				}
			}
			return;
		}
		int bytes, ri, gi, bi;
		Layout( format, out bytes, out ri, out gi, out bi );
		for ( int i = 0; i < w * h; i++ )
		{
			int p = o + i * bytes;
			r[ i ] = data[ p + ri ] / 255f; g[ i ] = data[ p + gi ] / 255f; b[ i ] = data[ p + bi ] / 255f;
		}
	}

	// Decodes one image's alpha (0..1) per texel (1 where the format has none)
	static void DecodeAlpha( byte[] data, int o, int w, int h, int format, float[] a )
	{
		if ( format == DXT1 || format == DXT1_ONEBITALPHA || format == DXT3 || format == DXT5 )
		{
			int blockSize = ( format == DXT3 || format == DXT5 ) ? 16 : 8;
			int bw = Math.Max( ( w + 3 ) / 4, 1 ), bh = Math.Max( ( h + 3 ) / 4, 1 );
			float[] palette = new float[ 8 ];
			for ( int by = 0; by < bh; by++ )
			{
				for ( int bx = 0; bx < bw; bx++ )
				{
					int at = o + ( by * bw + bx ) * blockSize;
					if ( format == DXT5 )
					{
						int a0 = data[ at ], a1 = data[ at + 1 ];
						palette[ 0 ] = a0 / 255f;
						palette[ 1 ] = a1 / 255f;
						for ( int k = 2; k < 8; k++ )
						{
							if ( a0 > a1 )
								palette[ k ] = ( ( 8 - k ) * a0 + ( k - 1 ) * a1 ) / 7f / 255f;
							else
								palette[ k ] = k < 6 ? ( ( 6 - k ) * a0 + ( k - 1 ) * a1 ) / 5f / 255f : ( k == 6 ? 0f : 1f );
						}
					}
					int c0 = Int16At( data, at + blockSize - 8 ), c1 = Int16At( data, at + blockSize - 6 );
					uint indices = (uint)Int32At( data, at + blockSize - 4 );
					long alphaBits = 0;	// DXT5: 16 indices of 3 bits
					for ( int k = 0; format == DXT5 && k < 6; k++ )
						alphaBits |= (long)data[ at + 2 + k ] << ( 8 * k );
					for ( int i = 0; i < 16; i++ )
					{
						int x = bx * 4 + ( i & 3 ), y = by * 4 + ( i >> 2 );
						if ( x >= w || y >= h )
							continue;
						float value = 1f;
						if ( format == DXT5 )
							value = palette[ (int)( ( alphaBits >> ( 3 * i ) ) & 7 ) ];
						else if ( format == DXT3 )
							value = ( ( data[ at + i / 2 ] >> ( 4 * ( i & 1 ) ) ) & 15 ) / 15f;
						else if ( c0 <= c1 && ( ( indices >> ( 2 * i ) ) & 3 ) == 3 )
							value = 0f;
						a[ y * w + x ] = value;
					}
				}
			}
			return;
		}
		int alphaAt = -1;
		switch ( format )
		{
			case RGBA8888: case BGRA8888: alphaAt = 3; break;
			case ABGR8888: case ARGB8888: alphaAt = 0; break;
		}
		int bytes = ( format == RGB888 || format == BGR888 ) ? 3 : 4;
		for ( int i = 0; i < w * h; i++ )
			a[ i ] = alphaAt < 0 ? 1f : data[ o + i * bytes + alphaAt ] / 255f;
	}

	// In-place radix-2 FFT of n complex values (re, im) with stride; inverse scales by 1/n
	static void Fft( double[] re, double[] im, int start, int stride, int n, bool inverse )
	{
		for ( int i = 1, j = 0; i < n; i++ )
		{
			int bit = n >> 1;
			for ( ; ( j & bit ) != 0; bit >>= 1 )
				j ^= bit;
			j ^= bit;
			if ( i < j )
			{
				int a = start + i * stride, b = start + j * stride;
				double t = re[ a ]; re[ a ] = re[ b ]; re[ b ] = t;
				t = im[ a ]; im[ a ] = im[ b ]; im[ b ] = t;
			}
		}
		for ( int len = 2; len <= n; len <<= 1 )
		{
			double angle = 2 * Math.PI / len * ( inverse ? 1 : -1 );
			double wr = Math.Cos( angle ), wi = Math.Sin( angle );
			for ( int i = 0; i < n; i += len )
			{
				double cr = 1, ci = 0;
				for ( int k = 0; k < len / 2; k++ )
				{
					int a = start + ( i + k ) * stride, b = start + ( i + k + len / 2 ) * stride;
					double xr = re[ b ] * cr - im[ b ] * ci, xi = re[ b ] * ci + im[ b ] * cr;
					re[ b ] = re[ a ] - xr; im[ b ] = im[ a ] - xi;
					re[ a ] += xr; im[ a ] += xi;
					double ncr = cr * wr - ci * wi;
					ci = cr * wi + ci * wr;
					cr = ncr;
				}
			}
		}
		if ( inverse )
		{
			for ( int i = 0; i < n; i++ )
			{
				re[ start + i * stride ] /= n;
				im[ start + i * stride ] /= n;
			}
		}
	}

	static void Fft2( double[] re, double[] im, int w, int h, bool inverse )
	{
		for ( int y = 0; y < h; y++ )
			Fft( re, im, y * w, 1, w, inverse );
		for ( int x = 0; x < w; x++ )
			Fft( re, im, x, w, h, inverse );
	}

	static bool PowerOfTwo( int n ) { return n > 0 && ( n & ( n - 1 ) ) == 0; }

	// The height map (.vtf, I8, all mip levels) for a normal map, or null with the reason
	public static byte[] Make( byte[] vtf, out string reason )
	{
		return Make( vtf, false, false, out reason );
	}

	// The same; ssbump: the material says it is a self-shadowed bump map
	// (the texture's own flag counts too)
	public static byte[] Make( byte[] vtf, bool ssbump, out string reason )
	{
		return Make( vtf, ssbump, false, out reason );
	}

	// The height map for a wall without a bump map, from its base texture
	// (see ColorHeight)
	public static byte[] MakeFromColor( byte[] vtf, out string reason )
	{
		return Make( vtf, false, true, false, out reason );
	}

	// The same for bricks, tiles and blocks: laid in courses (see CourseRelief)
	public static byte[] MakeFromCourses( byte[] vtf, out string reason )
	{
		return Make( vtf, false, true, true, out reason );
	}

	// The same; texture: its name (materials/brick/brickwall014b.vtf) - a
	// façade measured out (Facades) gets its sill, cornice, panel and window
	public static byte[] MakeFromCourses( byte[] vtf, string texture, out string reason )
	{
		s_Facade = FacadeOf( texture );
		try { return Make( vtf, false, true, true, out reason ); }
		finally { s_Facade = null; }
	}

	// The same for wood: boards with low gaps between them (see PlankRelief)
	public static byte[] MakeFromPlanks( byte[] vtf, out string reason )
	{
		return Make( vtf, false, true, false, true, false, null, out reason, false );
	}

	// The same for a bullet hole (decal): deep where it is dark and opaque (see DecalHeight)
	public static byte[] MakeFromDecal( byte[] vtf, out string reason )
	{
		return Make( vtf, false, true, false, true, null, out reason );
	}

	// The same for a decal atlas: holes only inside the given rectangles (x, y,
	// width, height in texels of the full-size texture: the bullet holes'
	// $Pos / $Size), the rest of the atlas (blood, scorch marks) stays flat
	public static byte[] MakeFromDecal( byte[] vtf, int[] rects, out string reason )
	{
		return Make( vtf, false, true, false, true, rects, out reason );
	}

	static byte[] Make( byte[] vtf, bool ssbump, bool fromColor, out string reason )
	{
		return Make( vtf, ssbump, fromColor, false, out reason );
	}

	static byte[] Make( byte[] vtf, bool ssbump, bool fromColor, bool courses, out string reason )
	{
		return Make( vtf, ssbump, fromColor, courses, false, null, out reason );
	}

	// A picture to look at (Julius sends it when a wall looks wrong): the
	// base texture on the left, the height made from it on the right, as a
	// .bmp - for walls without a bump map (kind as deploy.ps1's: color,
	// courses for bricks, tiles and blocks, planks for wood)
	public static byte[] PreviewFromColor( byte[] vtf, string kind, out string reason )
	{
		return Make( vtf, false, true, kind == "courses", kind == "planks", false, null, out reason, true );
	}

	// The same with the texture's name (see MakeFromCourses)
	public static byte[] PreviewFromColor( byte[] vtf, string kind, string texture, out string reason )
	{
		s_Facade = kind == "courses" ? FacadeOf( texture ) : null;
		try { return PreviewFromColor( vtf, kind, out reason ); }
		finally { s_Facade = null; }
	}

	static byte[] Make( byte[] vtf, bool ssbump, bool fromColor, bool courses, bool decal, int[] rects, out string reason )
	{
		return Make( vtf, ssbump, fromColor, courses, false, decal, rects, out reason, false );
	}

	static byte[] Make( byte[] vtf, bool ssbump, bool fromColor, bool courses, bool planks, bool decal, int[] rects, out string reason, bool preview )
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
		if ( minor < 0 || minor > 5 || headerSize < 64 || headerSize > vtf.Length || ( flags & FLAG_ENVMAP ) != 0 || depth != 1 )
		{
			reason = "not a plain normal map";
			return null;
		}
		if ( width < 4 || height < 4 || mips < 1 || mips > 17 || ImageSize( width, height, format ) < 0 || !PowerOfTwo( width ) || !PowerOfTwo( height ) )
		{
			reason = "format " + format + " or size not handled";
			return null;
		}
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

		// The largest level up to MAX_SIZE (smallest levels come first, each with all frames)
		int level = 0;
		while ( level + 1 < mips && ( ( width >> level ) > MAX_SIZE || ( height >> level ) > MAX_SIZE ) )
			level++;
		int w = Math.Max( width >> level, 1 ), h = Math.Max( height >> level, 1 );
		if ( w < 4 || h < 4 )
		{
			reason = "too small";
			return null;
		}
		if ( w > 4 * MAX_SIZE || h > 4 * MAX_SIZE )
		{
			// a huge texture without smaller mip levels: hundreds of MB and minutes for one wall
			reason = "too large";
			return null;
		}
		long offset = start;
		for ( int l = mips - 1; l > level; l-- )
			offset += frames * ImageSize( width >> l, height >> l, format );
		if ( offset + ImageSize( w, h, format ) > vtf.Length )
		{
			reason = "cut off";
			return null;
		}
		int n = w * h;
		float[] nr = new float[ n ], ng = new float[ n ], nb = new float[ n ];
		Decode( vtf, (int)offset, w, h, format, nr, ng, nb );
		if ( decal )
		{
			float[] na = new float[ n ];
			DecodeAlpha( vtf, (int)offset, w, h, format, na );
			// already 0..1 (the hole is small: by the 1 % and 99 % heights it was all top)
			double[] holeHeight;
			if ( rects == null )
				holeHeight = DecalHeight( nr, ng, nb, na, w, h );
			else
			{
				// each rectangle on its own (scaled to the level read), flat between them
				holeHeight = new double[ n ];
				for ( int i = 0; i < n; i++ )
					holeHeight[ i ] = 1.0;
				double scaleX = (double)w / width, scaleY = (double)h / height;
				for ( int k = 0; k + 3 < rects.Length; k += 4 )
				{
					int x0 = (int)Math.Floor( rects[ k ] * scaleX ), y0 = (int)Math.Floor( rects[ k + 1 ] * scaleY );
					int rw = Math.Min( (int)Math.Ceiling( rects[ k + 2 ] * scaleX ), w - x0 ), rh = Math.Min( (int)Math.Ceiling( rects[ k + 3 ] * scaleY ), h - y0 );
					if ( x0 < 0 || y0 < 0 || rw < 4 || rh < 4 )
						continue;
					float[] cr = new float[ rw * rh ], cg = new float[ rw * rh ], cb = new float[ rw * rh ], ca = new float[ rw * rh ];
					for ( int y = 0; y < rh; y++ )
					{
						for ( int x = 0; x < rw; x++ )
						{
							int from = ( y0 + y ) * w + x0 + x, to = y * rw + x;
							cr[ to ] = nr[ from ]; cg[ to ] = ng[ from ]; cb[ to ] = nb[ from ]; ca[ to ] = na[ from ];
						}
					}
					double[] cut = DecalHeight( cr, cg, cb, ca, rw, rh );
					for ( int y = 0; y < rh; y++ )
					{
						for ( int x = 0; x < rw; x++ )
							holeHeight[ ( y0 + y ) * w + x0 + x ] = cut[ y * rw + x ];
					}
				}
			}
			byte[] holeLevel = new byte[ n ];
			for ( int i = 0; i < n; i++ )
				holeLevel[ i ] = (byte)( Math.Max( 0.0, Math.Min( 1.0, holeHeight[ i ] ) ) * 255.0 + 0.5 );
			return WriteI8( holeLevel, w, h );
		}
		if ( fromColor )
		{
			double[] laid = courses ? CourseRelief( nr, ng, nb, w, h ) : null;
			double[] boards = planks ? PlankRelief( nr, ng, nb, w, h ) : null;
			byte[] made;
			if ( laid != null && s_Facade != null )
			{
				// a façade's parts: levels already 0..1, kept as they are
				byte[] facadeLevel = new byte[ n ];
				for ( int i = 0; i < n; i++ )
					facadeLevel[ i ] = (byte)( Math.Max( 0.0, Math.Min( 1.0, laid[ i ] ) ) * 255.0 + 0.5 );
				made = WriteI8( facadeLevel, w, h );
			}
			else if ( laid != null )
				made = ToI8( laid, w, h, false, out reason );
			else if ( boards != null )
			{
				// already 0..1 (the gaps are few: by the 1 % height the bottom
				// would lie in the grain)
				byte[] boardLevel = new byte[ n ];
				for ( int i = 0; i < n; i++ )
					boardLevel[ i ] = (byte)( Math.Max( 0.0, Math.Min( 1.0, boards[ i ] ) ) * 255.0 + 0.5 );
				made = WriteI8( boardLevel, w, h );
			}
			else
			{
				bool shaped;
				double[] heights = ColorHeight( nr, ng, nb, w, h, planks, out shaped );
				made = ToI8( heights, w, h, !shaped, out reason );
			}
			if ( !preview || made == null )
				return made;
			// the top level is the last w x h bytes of the .vtf
			byte[] top = new byte[ n ];
			Array.Copy( made, made.Length - n, top, 0, n );
			return SideBySide( nr, ng, nb, top, w, h );
		}
		if ( ssbump || ( flags & FLAG_SSBUMP ) != 0 )
		{
			// A self-shadowed bump map ($ssbump) holds how much of each of the
			// three lightmap directions a texel gets: their sum is its normal
			for ( int i = 0; i < n; i++ )
			{
				double x = 0.81649658 * nr[ i ] - 0.40824829 * ( ng[ i ] + nb[ i ] );
				double y = 0.70710678 * ( ng[ i ] - nb[ i ] );
				double z = 0.57735027 * ( nr[ i ] + ng[ i ] + nb[ i ] );
				double length = Math.Sqrt( x * x + y * y + z * z );
				if ( length < 1e-6 )
				{
					x = 0;
					y = 0;
					z = 1;
					length = 1;
				}
				nr[ i ] = (float)( ( x / length + 1 ) * 0.5 );
				ng[ i ] = (float)( ( y / length + 1 ) * 0.5 );
				nb[ i ] = (float)( ( z / length + 1 ) * 0.5 );
			}
		}

		// Slopes (texels: x right, y down in the texture)
		double[] p = new double[ n ], q = new double[ n ];
		for ( int i = 0; i < n; i++ )
		{
			double nx = nr[ i ] * 2 - 1, ny = ng[ i ] * 2 - 1, nz = Math.Max( nb[ i ] * 2 - 1, 0.2 );
			p[ i ] = -nx / nz;
			q[ i ] = -ny / nz;
		}
		// Green up or down: the one whose slopes fit together (least curl)
		double curlSame = 0, curlFlipped = 0;
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				int i = y * w + x, right = y * w + ( x + 1 ) % w, down = ( ( y + 1 ) % h ) * w + x;
				double dpdy = p[ down ] - p[ i ], dqdx = q[ right ] - q[ i ];
				curlSame += ( dpdy - dqdx ) * ( dpdy - dqdx );
				curlFlipped += ( dpdy + dqdx ) * ( dpdy + dqdx );
			}
		}
		// Source's own maps have green along the texture's v (down in the image,
		// vrad's bump basis): kept unless the other way fits clearly better
		// (bricks fit both ways: their rows and columns do not mix)
		if ( curlFlipped < curlSame * 0.7 )
		{
			for ( int i = 0; i < n; i++ )
				q[ i ] = -q[ i ];
		}

		// Frankot-Chellappa: H = -i (u P + v Q) / (u^2 + v^2), slow waves taken out
		double[] pr = (double[])p.Clone(), pi = new double[ n ], qr = (double[])q.Clone(), qi = new double[ n ];
		Fft2( pr, pi, w, h, false );
		Fft2( qr, qi, w, h, false );
		double[] hr = new double[ n ], hi = new double[ n ];
		for ( int y = 0; y < h; y++ )
		{
			double v = 2 * Math.PI * ( y <= h / 2 ? y : y - h ) / h;
			for ( int x = 0; x < w; x++ )
			{
				double u = 2 * Math.PI * ( x <= w / 2 ? x : x - w ) / w;
				double d = u * u + v * v;
				int i = y * w + x;
				if ( d < 1e-12 )
					continue;
				// waves a texture: cycles across it (the longer side)
				double cycles = Math.Sqrt( Math.Pow( ( x <= w / 2 ? x : x - w ), 2 ) + Math.Pow( ( y <= h / 2 ? y : y - h ) * (double)w / h, 2 ) );
				double keep = 1.0 - Math.Exp( -( cycles * cycles ) / 9.0 );
				// -i (u P + v Q) / d
				double ar = u * pr[ i ] + v * qr[ i ], ai = u * pi[ i ] + v * qi[ i ];
				hr[ i ] = ai / d * keep;
				hi[ i ] = -ar / d * keep;
			}
		}
		Fft2( hr, hi, w, h, true );

		return ToI8( hr, w, h, false, out reason );
	}

	// Heights to 0..1 by the 1 % and 99 % heights, as an I8 .vtf. plateau:
	// flatter tops and bottoms (a base texture's brightness also holds stains
	// and grain; bricks and stones are mostly flat on top)
	static byte[] ToI8( double[] heights, int w, int h, bool plateau, out string reason )
	{
		reason = null;
		int n = w * h;
		double[] sorted = (double[])heights.Clone();
		Array.Sort( sorted );
		double bottom = sorted[ (int)( n * 0.01 ) ], top = sorted[ (int)( n * 0.99 ) ];
		if ( top - bottom < 1e-9 )
		{
			reason = "flat";
			return null;
		}
		byte[] level0 = new byte[ n ];
		for ( int i = 0; i < n; i++ )
		{
			double t = Math.Max( 0.0, Math.Min( 1.0, ( heights[ i ] - bottom ) / ( top - bottom ) ) );
			if ( plateau )
			{
				t = Math.Max( 0.0, Math.Min( 1.0, ( t - 0.15 ) / 0.7 ) );
				t = t * t * ( 3 - 2 * t );
			}
			level0[ i ] = (byte)( t * 255.0 + 0.5 );
		}
		return WriteI8( level0, w, h );
	}

	// A wall without a bump map (most bricks, stones and tiles of a map):
	// its brightness, without the slow changes (light and stains across the
	// texture, as for the normal maps) and without the finest grain (it
	// would only make noise). Which way is up: the joints are the narrow
	// part - mortar, grout, gaps - and the narrow part is the tail of the
	// distribution: dark mortar between lighter bricks leaves a long dark
	// tail (skewed down, kept), light mortar a long light one (turned round).
	// darkLow: dark is always low, no joints looked for (wood without clear
	// gaps: its light speckles made the tail light, the relief stood inside
	// out, and its grain is no network of stones)
	static double[] ColorHeight( float[] r, float[] g, float[] b, int w, int h, bool darkLow, out bool shaped )
	{
		int n = w * h;
		double[] re = new double[ n ], im = new double[ n ];
		for ( int i = 0; i < n; i++ )
			re[ i ] = 0.299 * r[ i ] + 0.587 * g[ i ] + 0.114 * b[ i ];
		Fft2( re, im, w, h, false );
		double longest = Math.Max( w, h );
		// two copies: without the slow changes (decides which way is up: the
		// joints are thin, a filter against grain would blur them away), and
		// also without the finest grain (the height)
		double[] sr = new double[ n ], si = new double[ n ];
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				// waves a texture: cycles across it (the longer side)
				double cycles = Math.Sqrt( Math.Pow( ( x <= w / 2 ? x : x - w ) * longest / w, 2 ) + Math.Pow( ( y <= h / 2 ? y : y - h ) * longest / h, 2 ) );
				double slow = 1.0 - Math.Exp( -( cycles * cycles ) / 9.0 );
				double fine = cycles / ( longest * 0.35 );
				int i = y * w + x;
				sr[ i ] = re[ i ] * slow;
				si[ i ] = im[ i ] * slow;
				re[ i ] *= slow * Math.Exp( -fine * fine );
				im[ i ] *= slow * Math.Exp( -fine * fine );
			}
		}
		Fft2( re, im, w, h, true );
		Fft2( sr, si, w, h, true );

		double mean = 0, m3 = 0;
		for ( int i = 0; i < n; i++ )
			mean += sr[ i ];
		mean /= n;
		for ( int i = 0; i < n; i++ )
		{
			double d = sr[ i ] - mean;
			m3 += d * d * d;
		}
		if ( darkLow )
		{
			shaped = false;
			return re;
		}
		if ( m3 > 0 )
		{
			for ( int i = 0; i < n; i++ )
			{
				re[ i ] = -re[ i ];
				sr[ i ] = -sr[ i ];
			}
		}
		double[] relief = JointRelief( sr, re, w, h );
		shaped = relief != null;
		return shaped ? relief : re;
	}

	// Wood (boards, planks): the gaps between the boards low, the boards flat
	// on top with a little of their grain (Julius: "Holz sollte minimale
	// Parallax haben"; from the brightness alone the dark gaps stood out as
	// ridges - "das Schwarze wird raus statt das Holz"). The gaps are dark
	// lines along the whole board: the dark top-hat across the boards,
	// averaged along them, stands out at every gap, while the grain's streaks
	// wander and fade and average out. Upright and lying boards are both
	// tried. null without clear gaps.
	static double[] PlankRelief( float[] r, float[] g, float[] b, int w, int h )
	{
		int n = w * h;
		double[] lum = new double[ n ];
		for ( int i = 0; i < n; i++ )
			lum[ i ] = 0.299 * r[ i ] + 0.587 * g[ i ] + 0.114 * b[ i ];
		double scoreUpright, scoreLying;
		double[] upright = PlankColumns( lum, w, h, out scoreUpright );
		double[] lying = PlankColumns( Transpose( lum, w, h ), h, w, out scoreLying );
		if ( lying != null && ( upright == null || scoreLying > scoreUpright ) )
			return Transpose( lying, h, w );
		return upright;
	}

	// Boards running down the image (gaps are columns); see PlankRelief
	static double[] PlankColumns( double[] lum, int w, int h, out double score )
	{
		score = 0;
		int n = w * h;
		// dark lines across the boards up to about 3 % of the width wide
		int radius = Math.Max( 2, w / 64 );
		double[] closed = SquareFilterAcross( SquareFilterAcross( lum, w, h, radius, true ), w, h, radius, false );
		double[] dark = new double[ n ];
		double[] profile = new double[ w ];
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				int i = y * w + x;
				dark[ i ] = Math.Max( 0.0, closed[ i ] - lum[ i ] );
				profile[ x ] += dark[ i ] / h;
			}
		}
		// the usual column, and how far columns usually stray from it
		double[] sorted = (double[])profile.Clone();
		Array.Sort( sorted );
		double median = sorted[ w / 2 ];
		double[] stray = new double[ w ];
		for ( int x = 0; x < w; x++ )
			stray[ x ] = Math.Abs( profile[ x ] - median );
		Array.Sort( stray );
		double spread = Math.Max( stray[ w / 2 ], 1e-4 );

		// gaps: columns far above the usual (6 x the spread, at least 3 % darker
		// than the wood around), strongest first, a gap's width apart, dark in
		// at least 60 % of the rows (the whole board long)
		int[] order = new int[ w ];
		for ( int x = 0; x < w; x++ )
			order[ x ] = x;
		Array.Sort( order, ( a, c ) => profile[ c ].CompareTo( profile[ a ] ) );
		var centres = new System.Collections.Generic.List<int>();
		var halves = new System.Collections.Generic.List<double>();
		int spacing = Math.Max( 4, w / 32 );
		double strength = 0;
		foreach ( int x0 in order )
		{
			double rise = profile[ x0 ] - median;
			if ( rise < 6 * spread || rise < 0.03 )
				break;
			bool near = false;
			foreach ( int c in centres )
				near |= Math.Min( Math.Abs( c - x0 ), w - Math.Abs( c - x0 ) ) < spacing;
			if ( near )
				continue;
			int rows = 0;
			for ( int y = 0; y < h; y++ )
			{
				double most = 0;
				for ( int d = -1; d <= 1; d++ )
					most = Math.Max( most, dark[ y * w + ( x0 + d + w ) % w ] );
				rows += most > 0.5 * rise ? 1 : 0;
			}
			if ( rows < h * 0.6 )
				continue;
			// its width: where it is above half its rise
			int left = 0, right = 0;
			while ( left < 3 * radius && profile[ ( x0 - left - 1 + w ) % w ] - median > 0.5 * rise )
				left++;
			while ( right < 3 * radius && profile[ ( x0 + right + 1 ) % w ] - median > 0.5 * rise )
				right++;
			centres.Add( x0 );
			halves.Add( 0.5 * ( left + right + 1 ) );
			strength += rise / spread * rows / h;
		}
		if ( centres.Count == 0 )
			return null;
		score = strength / centres.Count;

		// how much of each gap is there in each row (smoothed along it: grain
		// across a gap must not cut it)
		int along = Math.Max( 2, h / 32 );
		double[][] presence = new double[ centres.Count ][];
		for ( int k = 0; k < centres.Count; k++ )
		{
			double[] row = new double[ h ];
			int reach = (int)Math.Ceiling( halves[ k ] );
			double rise = profile[ centres[ k ] ] - median;
			for ( int y = 0; y < h; y++ )
			{
				double most = 0;
				for ( int d = -reach; d <= reach; d++ )
					most = Math.Max( most, dark[ y * w + ( centres[ k ] + d + w * 4 ) % w ] );
				row[ y ] = Math.Min( 1.0, most / rise );
			}
			presence[ k ] = new double[ h ];
			for ( int y = 0; y < h; y++ )
			{
				double sum = 0;
				for ( int d = -along; d <= along; d++ )
					sum += row[ ( y + d + h * 4 ) % h ];
				presence[ k ][ y ] = sum / ( 2 * along + 1 );
			}
		}

		// the grain: brightness without the slow changes, a little on top (dark low)
		double[] detail = Blur3( lum, w, h );
		double[] slow = BoxMean( lum, w, h, Math.Max( 2, w / 16 ) );
		double variance = 0;
		for ( int i = 0; i < n; i++ )
		{
			detail[ i ] -= slow[ i ];
			variance += detail[ i ] * detail[ i ] / n;
		}
		double deviation = Math.Max( Math.Sqrt( variance ), 1e-6 );
		const double grainShare = 0.1;
		double bevel = Math.Max( 1.0, w / 256.0 );

		double[] relief = new double[ n ];
		for ( int x = 0; x < w; x++ )
		{
			// the nearest gap: a groove with a small bevel at the board edges
			int nearest = 0;
			double distance = double.MaxValue;
			for ( int k = 0; k < centres.Count; k++ )
			{
				int d = Math.Abs( centres[ k ] - x );
				d = Math.Min( d, w - d );
				if ( d < distance )
				{
					distance = d;
					nearest = k;
				}
			}
			double t = Math.Max( 0.0, Math.Min( 1.0, ( distance - 0.5 * halves[ nearest ] ) / ( 0.5 * halves[ nearest ] + bevel ) ) );
			double groove = 1.0 - t * t * ( 3 - 2 * t );
			for ( int y = 0; y < h; y++ )
			{
				int i = y * w + x;
				double grain = Math.Max( 0.0, Math.Min( 1.0, 0.5 + detail[ i ] / ( 4 * deviation ) ) );
				double top = 1.0 - grainShare + grainShare * grain;
				relief[ i ] = top * ( 1.0 - groove * presence[ nearest ][ y ] );
			}
		}
		return relief;
	}

	// Stones from their joints, as a mason would see them: brightness alone
	// made every grain and stain a bump (Julius: "sehr kribbelig", the bricks'
	// own shape not there). The joints (the low side, split from the stones
	// by Otsu's threshold on the lightly smoothed image) must form a network:
	// specks smaller than a quarter of a percent of the texture are grain and
	// dropped. Each stone is then flat on top with a rounded edge down to the
	// joints (distance from the nearest joint over a bevel of 0.8 x the
	// stones' mean distance, at most 4 % of the texture), the grain only a
	// little on top (15 %). null where there are no clear joints (rock,
	// plaster: 3 to 50 % of the texture must be joints) - those keep the
	// brightness relief.
	static double[] JointRelief( double[] edges, double[] detail, int w, int h )
	{
		int n = w * h;
		double[] smooth = Blur3( edges, w, h );
		double low = double.MaxValue, high = double.MinValue;
		for ( int i = 0; i < n; i++ )
		{
			low = Math.Min( low, smooth[ i ] );
			high = Math.Max( high, smooth[ i ] );
		}
		if ( high - low < 1e-9 )
			return null;

		// Otsu: the split with the largest variance between the two sides
		int[] histogram = new int[ 256 ];
		for ( int i = 0; i < n; i++ )
			histogram[ Math.Min( 255, (int)( ( smooth[ i ] - low ) / ( high - low ) * 256.0 ) ) ]++;
		double total = 0;
		for ( int k = 0; k < 256; k++ )
			total += k * (double)histogram[ k ];
		double below = 0, belowSum = 0, best = -1;
		int split = 128;
		for ( int k = 0; k < 256; k++ )
		{
			below += histogram[ k ];
			belowSum += k * (double)histogram[ k ];
			double above = n - below;
			if ( below < 1 || above < 1 )
				continue;
			double meanBelow = belowSum / below, meanAbove = ( total - belowSum ) / above;
			double between = below * above * ( meanBelow - meanAbove ) * ( meanBelow - meanAbove );
			if ( between > best )
			{
				best = between;
				split = k;
			}
		}
		double threshold = low + ( split + 1 ) / 256.0 * ( high - low );
		bool[] joint = new bool[ n ];
		for ( int i = 0; i < n; i++ )
			joint[ i ] = smooth[ i ] < threshold;
		RemoveSmall( joint, w, h, Math.Max( 8, n / 400 ) );
		int joints = 0;
		for ( int i = 0; i < n; i++ )
			joints += joint[ i ] ? 1 : 0;
		if ( joints < n * 0.03 || joints > n * 0.5 )
			return null;

		return ShapeStones( joint, joints, detail, w, h );
	}

	// A bullet hole (decal): deep where it is dark and opaque - the hole in
	// the middle; the clear surroundings and the border stay at the top. Dark
	// against the decal's brightest opaque part (95 %), smoothed twice, the
	// deepest 1 % at the bottom, a little rounder towards the middle.
	static double[] DecalHeight( float[] r, float[] g, float[] b, float[] a, int w, int h )
	{
		int n = w * h;
		var opaque = new System.Collections.Generic.List<double>();
		double[] lum = new double[ n ];
		for ( int i = 0; i < n; i++ )
		{
			lum[ i ] = 0.299 * r[ i ] + 0.587 * g[ i ] + 0.114 * b[ i ];
			if ( a[ i ] > 0.5 )
				opaque.Add( lum[ i ] );
		}
		if ( opaque.Count == 0 )
			opaque.Add( 1.0 );
		opaque.Sort();
		double bright = Math.Max( opaque[ (int)( opaque.Count * 0.95 ) ], 0.05 );
		double[] hole = new double[ n ];
		for ( int i = 0; i < n; i++ )
			hole[ i ] = a[ i ] * Math.Max( 0.0, 1.0 - lum[ i ] / bright );
		hole = Blur3( Blur3( hole, w, h ), w, h );
		double[] sorted = (double[])hole.Clone();
		Array.Sort( sorted );
		double deepest = Math.Max( sorted[ (int)( n * 0.99 ) ], 1e-6 );
		double[] height = new double[ n ];
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				int i = y * w + x;
				bool border = x == 0 || y == 0 || x == w - 1 || y == h - 1;	// decals clamp: the edge stays on top
				height[ i ] = border ? 1.0 : 1.0 - Math.Pow( Math.Min( hole[ i ] / deepest, 1.0 ), 0.8 );
			}
		}
		return height;
	}

	// Each stone from its joints: distance from the nearest joint over a
	// bevel of 0.8 x the stones' mean distance (at most 4 % of the texture),
	// rounded edge, flat top, the grain only a little on top (15 %), 3 x 3 smoothed
	static double[] ShapeStones( bool[] joint, int joints, double[] detail, int w, int h )
	{
		return ShapeStones( joint, joints, detail, w, h, 0.8, 0.04, 0.15, false );
	}

	// The same with the bevel (share of the mean distance, at most a share of
	// the texture), the grain's share, and crisp: a straight chamfer instead
	// of a rounded edge
	static double[] ShapeStones( bool[] joint, int joints, double[] detail, int w, int h, double bevelShare, double bevelMax,
		double grainShare, bool crisp )
	{
		int n = w * h;
		double[] distance = Distance( joint, w, h );
		// the mean distance to a joint, over the stones only: up to 3 x the
		// median distance (a façade's smooth panel and window, far from any
		// joint, made it large and every brick a cushion)
		double[] stoneDistances = new double[ Math.Max( n - joints, 1 ) ];
		int stones = 0;
		for ( int i = 0; i < n && stones < stoneDistances.Length; i++ )
		{
			if ( !joint[ i ] )
				stoneDistances[ stones++ ] = distance[ i ];
		}
		Array.Sort( stoneDistances, 0, stones );
		double median = stones > 0 ? stoneDistances[ stones / 2 ] : 0.0;
		double stoneSum = 0;
		int stoneCount = 0;
		for ( int i = 0; i < stones; i++ )
		{
			if ( stoneDistances[ i ] > 3.0 * median )
				break;
			stoneSum += stoneDistances[ i ];
			stoneCount++;
		}
		double bevel = Math.Max( 1.5, Math.Min( bevelShare * stoneSum / Math.Max( stoneCount, 1 ), Math.Max( w, h ) * bevelMax ) );

		// the grain, 0..1 by its 1 % and 99 % values
		double[] sorted = (double[])detail.Clone();
		Array.Sort( sorted );
		double bottom = sorted[ (int)( n * 0.01 ) ], top = Math.Max( sorted[ (int)( n * 0.99 ) ], bottom + 1e-9 );
		double[] relief = new double[ n ];
		for ( int i = 0; i < n; i++ )
		{
			double t = Math.Min( distance[ i ] / bevel, 1.0 );
			double shape = crisp ? t * t * ( 3 - 2 * t ) : 1.0 - ( 1.0 - t ) * ( 1.0 - t );	// chamfer or rounded edge, flat top
			double grain = Math.Max( 0.0, Math.Min( 1.0, ( detail[ i ] - bottom ) / ( top - bottom ) ) );
			relief[ i ] = shape * ( 1.0 - grainShare + grainShare * grain );
		}
		return Blur3( relief, w, h );
	}

	// Bricks, tiles, blocks: their joints found where they are, not from a
	// grid laid over the whole texture. A joint is mortar: greyer than the
	// bricks right around it and lighter or darker (whichever gives thin
	// lines; luminance and saturation each against its mean within half a
	// course). Bed joints are long lines across
	// (that response averaged along the row, two courses wide), head joints
	// short upright ones (averaged a course high); each the strongest share of
	// its response. Only where bricks are: busy at that scale, neither black nor
	// nearly white - a façade's window (dark or bright glass), plaster panel and
	// smooth trim get no joints and stay flat. The grid search before (one period and phase for the whole
	// texture) took a façade's window bars for courses and its bricks for
	// upright grooves (Julius 26.10.2026: "immer noch vertikal statt
	// horizontal", the wall full of "weirde Einkerbungen"). null when hardly
	// anything looks like bricks (then the generic way, ColorHeight).
	static double[] CourseRelief( float[] r, float[] g, float[] b, int w, int h )
	{
		int n = w * h;
		double[] lum = new double[ n ], sat = new double[ n ];
		for ( int i = 0; i < n; i++ )
		{
			lum[ i ] = 0.299 * r[ i ] + 0.587 * g[ i ] + 0.114 * b[ i ];
			double max = Math.Max( r[ i ], Math.Max( g[ i ], b[ i ] ) ), min = Math.Min( r[ i ], Math.Min( g[ i ], b[ i ] ) );
			sat[ i ] = ( max - min ) / Math.Max( max, 1e-3 );
		}
		// the scale: about half a course, the course height from the strongest
		// repeat of the rows' brightness - its first peak nearly as strong as the
		// highest (in a running bond two courses repeat a little more clearly)
		int radius = Math.Max( 2, Math.Min( w, h ) / 64 );
		int course = CourseHeight( lum, w, h );
		if ( course > 0 )
			radius = Math.Max( radius, (int)Math.Round( 0.4 * course ) );
		double[] lumAround = BoxMean( lum, w, h, radius ), satAround = BoxMean( sat, w, h, radius );
		double[] lumHigh = new double[ n ], satHigh = new double[ n ], busyIn = new double[ n ];
		for ( int i = 0; i < n; i++ )
		{
			lumHigh[ i ] = lum[ i ] - lumAround[ i ];
			satHigh[ i ] = sat[ i ] - satAround[ i ];
			busyIn[ i ] = Math.Abs( lumHigh[ i ] );
		}
		double lumSpread = Math.Max( Spread( lumHigh ), 1e-6 ), satSpread = Math.Max( Spread( satHigh ), 1e-6 );

		// where bricks are: busy at their scale (against their brightness - under
		// a stain too), not dark (glass) and not nearly white (bright glass)
		double[] busy = BoxMean( busyIn, w, h, 2 * radius ), lit = BoxMean( lum, w, h, 2 * radius );
		bool[] bricks = new bool[ n ];
		int brickCount = 0;
		for ( int i = 0; i < n; i++ )
		{
			double brickness = Clamp01( ( busy[ i ] / Math.Max( lit[ i ], 0.02 ) - 0.06 ) / 0.06 )
				* Clamp01( ( lit[ i ] - 0.08 ) / 0.06 ) * Clamp01( ( 0.75 - lit[ i ] ) / 0.1 );
			bricks[ i ] = brickness > 0.5;
			brickCount += bricks[ i ] ? 1 : 0;
		}
		if ( brickCount < n * 0.1 )
			return null;

		// Mortar is greyer than the bricks, lighter (Half-Life 2's façades) or
		// darker - as the greyer texels are
		double greyLight = 0;
		for ( int i = 0; i < n; i++ )
		{
			if ( bricks[ i ] )
				greyLight -= lumHigh[ i ] * satHigh[ i ];
		}
		double polarity = greyLight >= 0 ? 1.0 : -1.0;
		double[] mortar = new double[ n ];
		for ( int i = 0; i < n; i++ )
			mortar[ i ] = polarity * lumHigh[ i ] / lumSpread - satHigh[ i ] / satSpread;

		// bed joints: lines across (two courses wide), the rows standing out
		// (Otsu), at most 22 % of the bricks; head joints: short lines upright
		// (a course high), the strongest tenth of the rest
		double[] across = BoxMeanAlong( mortar, w, h, 2 * radius, true );
		double[] upright = BoxMeanAlong( mortar, w, h, radius, false );
		double bedLevel = Math.Max( Otsu( across, bricks ), ShareAbove( across, bricks, 0.22 ) );
		bool[] rest = new bool[ n ];
		for ( int i = 0; i < n; i++ )
			rest[ i ] = bricks[ i ] && across[ i ] <= bedLevel;
		double headLevel = ShareAbove( upright, rest, 0.10 ), headMortar = ShareAbove( mortar, rest, 0.5 );
		bool[] joint = new bool[ n ];
		for ( int i = 0; i < n; i++ )
			joint[ i ] = bricks[ i ] && ( across[ i ] > bedLevel || ( rest[ i ] && upright[ i ] > headLevel && mortar[ i ] > headMortar ) );
		// single specks (a stain, a chip) are no joints: at least 6 joint texels within 5 x 5
		bool[] kept = new bool[ n ];
		int joints = 0;
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				int i = y * w + x;
				if ( !joint[ i ] )
					continue;
				int near = 0;
				for ( int dy = -2; dy <= 2; dy++ )
				{
					for ( int dx = -2; dx <= 2; dx++ )
						near += joint[ ( ( y + dy + h ) % h ) * w + ( x + dx + w ) % w ] ? 1 : 0;
				}
				kept[ i ] = near >= 6;
				joints += kept[ i ] ? 1 : 0;
			}
		}
		if ( joints < n * 0.02 || joints > n * 0.5 )
			return null;
		// Bricks: flat faces, nearly sharp edges, little grain - a rounded edge
		// over most of the brick made every brick a cushion (Julius: "Noppen")
		double[] relief = ShapeStones( kept, joints, lum, w, h, 0.3, 0.012, 0.08, true );
		return s_Facade != null ? FacadeRelief( relief, lum, w, h, s_Facade ) : relief;
	}

	// ---- Façades measured out ----
	// A façade's sill, cornice, dentils, plaster panel and window cannot be
	// told from bricks by colour or grain alone (a detector tried on 49 of the
	// game's brick textures took the window panes for plaster and missed the
	// dentils). So the façades Julius' maps show are measured once by hand
	// (each row and column read off the texture) and their parts laid over the
	// bricks: what stands out (cornice, sill, pilasters, dentils) higher than
	// the bricks, the plaster panel a little lower and flat (it showed brick
	// joints - Julius 26.10.2026: "das Concrete-Ding hat ab und zu eine
	// Ziegelstein-Deformierung"), the window's glass deep behind its frame.
	// The parts are in 1/512 of the texture (any size works), inclusive.

	const int PART_LEVEL = 0;	// the level given
	const int PART_BRICKS = 1;	// the bricks, raised by the level (pilasters)
	const int PART_WINDOW = 2;	// glass (dark texels) at the level, the frame and bars at the bricks' face

	struct FacadePart
	{
		public int Kind, X0, Y0, X1, Y1, Step, Count;
		public double Level;
		public FacadePart( int kind, int x0, int y0, int x1, int y1, double level, int step = 0, int count = 1 )
		{
			Kind = kind; X0 = x0; Y0 = y0; X1 = x1; Y1 = y1; Level = level; Step = step; Count = count;
		}
	}

	static FacadePart Level( int x0, int y0, int x1, int y1, double level, int step = 0, int count = 1 )
	{
		return new FacadePart( PART_LEVEL, x0, y0, x1, y1, level, step, count );
	}

	// Heights of the bricks on a façade: joints 0.15, faces 0.7; the rest of
	// the range is for what stands out
	const double FACADE_JOINT = 0.15, FACADE_FACE = 0.7;

	static readonly System.Collections.Generic.Dictionary<string, FacadePart[]> Facades =
		new System.Collections.Generic.Dictionary<string, FacadePart[]>
	{
		// Half-Life 2's brickwall014b: pilasters left and right (x 448..63,
		// across the edge), a cornice with dentils on top, a window with a
		// sill, a plaster panel under it. Light falls from the upper left in
		// the painting: the dark rows under a part are its shadow (deep).
		{ "brick/brickwall014b", new[] {
			new FacadePart( PART_BRICKS, 448, 58, 511, 511, 0.1 ),
			new FacadePart( PART_BRICKS, 0, 58, 63, 511, 0.1 ),
			// the cornice, row by row: cap, the shadow under it, a bead, a
			// moulding, its shadow, the dentil band, a fillet, its shadow
			Level( 0, 0, 511, 0, 0.85 ),
			Level( 0, 1, 511, 8, 1.0 ),
			Level( 0, 9, 511, 10, 0.5 ),
			Level( 0, 11, 511, 14, 0.35 ),
			Level( 0, 15, 511, 17, 0.8 ),
			Level( 0, 18, 511, 18, 0.45 ),
			Level( 0, 19, 511, 21, 0.9 ),
			Level( 0, 22, 511, 25, 0.4 ),
			Level( 0, 26, 511, 51, 0.9 ),				// over the pilasters: one block
			Level( 64, 26, 447, 51, 0.6 ),				// behind the dentils
			Level( 68, 26, 79, 46, 0.92, 28, 14 ),		// 14 dentils, 12 wide, every 28
			Level( 0, 52, 511, 54, 0.85 ),
			Level( 0, 55, 511, 57, 0.4 ),
			// the window: frame x 96..411, rows 124..351; the sill under it
			// and its shadow; the plaster panel (its dark rim a step lower)
			new FacadePart( PART_WINDOW, 96, 124, 411, 351, 0.3 ),
			Level( 88, 352, 422, 362, 0.95 ),
			Level( 88, 363, 422, 368, 0.5 ),
			Level( 96, 384, 414, 480, 0.55 ),
			Level( 98, 386, 412, 477, 0.6 ),
		} },
	};

	[ThreadStatic] static FacadePart[] s_Facade;

	// The measured parts of a texture (materials/brick/brickwall014b.vtf), or null
	static FacadePart[] FacadeOf( string texture )
	{
		if ( string.IsNullOrEmpty( texture ) )
			return null;
		string name = texture.Replace( '\\', '/' ).ToLowerInvariant();
		if ( name.StartsWith( "materials/" ) )
			name = name.Substring( 10 );
		if ( name.EndsWith( ".vtf" ) )
			name = name.Substring( 0, name.Length - 4 );
		FacadePart[] parts;
		return Facades.TryGetValue( name, out parts ) ? parts : null;
	}

	// The bricks' relief (0..1) with a façade's parts laid over it, 0..1;
	// the parts' edges softened over a texel (no stair steps)
	static double[] FacadeRelief( double[] relief, double[] lum, int w, int h, FacadePart[] parts )
	{
		int n = w * h;
		double[] level = new double[ n ], weight = new double[ n ], bricks = new double[ n ];
		for ( int i = 0; i < n; i++ )
			bricks[ i ] = FACADE_JOINT + ( FACADE_FACE - FACADE_JOINT ) * relief[ i ];
		foreach ( FacadePart part in parts )
		{
			for ( int k = 0; k < part.Count; k++ )
			{
				int x0 = ( part.X0 + k * part.Step ) * w / 512, x1 = ( ( part.X1 + k * part.Step + 1 ) * w ) / 512 - 1;
				int y0 = part.Y0 * h / 512, y1 = ( ( part.Y1 + 1 ) * h ) / 512 - 1;
				for ( int y = Math.Max( y0, 0 ); y <= Math.Min( y1, h - 1 ); y++ )
				{
					for ( int x = Math.Max( x0, 0 ); x <= Math.Min( x1, w - 1 ); x++ )
					{
						int i = y * w + x;
						if ( part.Kind == PART_BRICKS )
						{
							bricks[ i ] += part.Level;
							continue;
						}
						if ( part.Kind == PART_WINDOW )
							level[ i ] = lum[ i ] < 0.12 ? part.Level : FACADE_FACE;
						else
							level[ i ] = part.Level;
						weight[ i ] = 1.0;
					}
				}
			}
		}
		double[] weighted = new double[ n ];
		for ( int i = 0; i < n; i++ )
			weighted[ i ] = level[ i ] * weight[ i ];
		weighted = Blur3( weighted, w, h );
		double[] soft = Blur3( weight, w, h );
		double[] result = new double[ n ];
		for ( int i = 0; i < n; i++ )
		{
			double part = soft[ i ] > 1e-6 ? weighted[ i ] / soft[ i ] : 0.0;
			result[ i ] = bricks[ i ] * ( 1.0 - soft[ i ] ) + part * soft[ i ];
		}
		return result;
	}

	// The course height of bricks: the first peak of the rows' brightness
	// autocorrelation nearly as strong as the strongest (0: none clear)
	static int CourseHeight( double[] lum, int w, int h )
	{
		double[] row = new double[ h ];
		double mean = 0;
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
				row[ y ] += lum[ y * w + x ] / w;
			mean += row[ y ] / h;
		}
		double power = 0;
		for ( int y = 0; y < h; y++ )
		{
			row[ y ] -= mean;
			power += row[ y ] * row[ y ];
		}
		if ( power <= 1e-12 )
			return 0;
		int last = h / 3 + 1;
		double[] c = new double[ last + 1 ];
		for ( int lag = 0; lag <= last; lag++ )
		{
			double sum = 0;
			for ( int y = 0; y < h; y++ )
				sum += row[ y ] * row[ ( y + lag ) % h ];
			c[ lag ] = sum / power;
		}
		double top = 0;
		for ( int lag = 3; lag < last; lag++ )
		{
			if ( c[ lag ] > c[ lag - 1 ] && c[ lag ] >= c[ lag + 1 ] && c[ lag ] > top )
				top = c[ lag ];
		}
		if ( top <= 0.2 )
			return 0;
		for ( int lag = 3; lag < last; lag++ )
		{
			if ( c[ lag ] > c[ lag - 1 ] && c[ lag ] >= c[ lag + 1 ] && c[ lag ] >= 0.6 * top )
				return lag;
		}
		return 0;
	}

	// Otsu's level splitting the values where mask is set into two groups
	static double Otsu( double[] values, bool[] mask )
	{
		double low = double.MaxValue, high = double.MinValue;
		for ( int i = 0; i < values.Length; i++ )
		{
			if ( !mask[ i ] )
				continue;
			low = Math.Min( low, values[ i ] );
			high = Math.Max( high, values[ i ] );
		}
		if ( high <= low )
			return low;
		const int bins = 128;
		double[] count = new double[ bins ];
		double step = ( high - low ) / bins;
		for ( int i = 0; i < values.Length; i++ )
		{
			if ( mask[ i ] )
				count[ Math.Min( bins - 1, (int)( ( values[ i ] - low ) / step ) ) ]++;
		}
		double total = 0, totalSum = 0;
		for ( int k = 0; k < bins; k++ )
		{
			total += count[ k ];
			totalSum += count[ k ] * ( low + ( k + 0.5 ) * step );
		}
		double below = 0, belowSum = 0, best = -1, level = low;
		for ( int k = 0; k < bins; k++ )
		{
			double centre = low + ( k + 0.5 ) * step;
			below += count[ k ];
			belowSum += count[ k ] * centre;
			double above = total - below;
			if ( below <= 0 || above <= 0 )
				continue;
			double difference = belowSum / below - ( totalSum - belowSum ) / above;
			double between = below * above * difference * difference;
			if ( between > best )
			{
				best = between;
				level = centre;
			}
		}
		return level;
	}

	static double Clamp01( double value )
	{
		return Math.Max( 0.0, Math.Min( 1.0, value ) );
	}

	// The spread of values around 0 (root mean square)
	static double Spread( double[] values )
	{
		double sum = 0;
		foreach ( double value in values )
			sum += value * value;
		return Math.Sqrt( sum / Math.Max( values.Length, 1 ) );
	}

	// The level the given share of the values where mask is set lies above
	static double ShareAbove( double[] values, bool[] mask, double share )
	{
		var picked = new System.Collections.Generic.List<double>();
		for ( int i = 0; i < values.Length; i++ )
		{
			if ( mask[ i ] )
				picked.Add( values[ i ] );
		}
		if ( picked.Count == 0 )
			return double.MaxValue;
		picked.Sort();
		return picked[ Math.Min( picked.Count - 1, (int)( picked.Count * ( 1.0 - share ) ) ) ];
	}

	// The mean over 2 x radius + 1 texels along rows (across) or columns, wrapping (textures tile)
	static double[] BoxMeanAlong( double[] values, int w, int h, int radius, bool acrossRows )
	{
		double[] result = new double[ values.Length ];
		double size = 2 * radius + 1;
		int lines = acrossRows ? h : w, length = acrossRows ? w : h;
		for ( int line = 0; line < lines; line++ )
		{
			Func<int, int> at = k => acrossRows ? line * w + ( ( k % w ) + w ) % w : ( ( ( k % h ) + h ) % h ) * w + line;
			double sum = 0;
			for ( int d = -radius; d <= radius; d++ )
				sum += values[ at( d ) ];
			for ( int k = 0; k < length; k++ )
			{
				result[ at( k ) ] = sum / size;
				sum += values[ at( k + radius + 1 ) ] - values[ at( k - radius ) ];
			}
		}
		return result;
	}

	// Mean over a square of (2 radius + 1) texels, wrapping (separable running sums)
	static double[] BoxMean( double[] values, int w, int h, int radius )
	{
		double[] across = new double[ values.Length ], result = new double[ values.Length ];
		double size = 2 * radius + 1;
		for ( int y = 0; y < h; y++ )
		{
			double sum = 0;
			for ( int d = -radius; d <= radius; d++ )
				sum += values[ y * w + ( d + w * 4 ) % w ];
			for ( int x = 0; x < w; x++ )
			{
				across[ y * w + x ] = sum / size;
				sum += values[ y * w + ( x + radius + 1 ) % w ] - values[ y * w + ( x - radius + w * 4 ) % w ];
			}
		}
		for ( int x = 0; x < w; x++ )
		{
			double sum = 0;
			for ( int d = -radius; d <= radius; d++ )
				sum += across[ ( ( d + h * 4 ) % h ) * w + x ];
			for ( int y = 0; y < h; y++ )
			{
				result[ y * w + x ] = sum / size;
				sum += across[ ( ( y + radius + 1 ) % h ) * w + x ] - across[ ( ( y - radius + h * 4 ) % h ) * w + x ];
			}
		}
		return result;
	}

	// Rows become columns (an image w x h becomes h x w)
	static double[] Transpose( double[] values, int w, int h )
	{
		double[] result = new double[ values.Length ];
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
				result[ x * h + y ] = values[ y * w + x ];
		}
		return result;
	}

	static bool[] Transpose( bool[] values, int w, int h )
	{
		if ( values == null )
			return null;
		bool[] result = new bool[ values.Length ];
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
				result[ x * h + y ] = values[ y * w + x ];
		}
		return result;
	}

	// Largest / smallest value along the row only (thin upright structures), wrapping
	static double[] SquareFilterAcross( double[] values, int w, int h, int radius, bool max )
	{
		double[] result = new double[ values.Length ];
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				double v = values[ y * w + x ];
				for ( int d = -radius; d <= radius; d++ )
				{
					double o = values[ y * w + ( x + d + w * 4 ) % w ];
					v = max ? Math.Max( v, o ) : Math.Min( v, o );
				}
				result[ y * w + x ] = v;
			}
		}
		return result;
	}

	// 3 x 3 box blur, wrapping (textures tile)
	static double[] Blur3( double[] values, int w, int h )
	{
		double[] result = new double[ values.Length ];
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				double sum = 0;
				for ( int dy = -1; dy <= 1; dy++ )
				{
					for ( int dx = -1; dx <= 1; dx++ )
						sum += values[ ( ( y + dy + h ) % h ) * w + ( x + dx + w ) % w ];
				}
				result[ y * w + x ] = sum / 9.0;
			}
		}
		return result;
	}

	// Drops the parts of a mask (8-connected, wrapping) smaller than minSize texels
	static void RemoveSmall( bool[] mask, int w, int h, int minSize )
	{
		int n = w * h;
		bool[] seen = new bool[ n ];
		int[] stack = new int[ n ];
		int[] part = new int[ n ];
		for ( int start = 0; start < n; start++ )
		{
			if ( !mask[ start ] || seen[ start ] )
				continue;
			int count = 0, top = 0;
			stack[ top++ ] = start;
			seen[ start ] = true;
			while ( top > 0 )
			{
				int i = stack[ --top ];
				part[ count++ ] = i;
				int x = i % w, y = i / w;
				for ( int dy = -1; dy <= 1; dy++ )
				{
					for ( int dx = -1; dx <= 1; dx++ )
					{
						int j = ( ( y + dy + h ) % h ) * w + ( x + dx + w ) % w;
						if ( mask[ j ] && !seen[ j ] )
						{
							seen[ j ] = true;
							stack[ top++ ] = j;
						}
					}
				}
			}
			if ( count < minSize )
			{
				for ( int k = 0; k < count; k++ )
					mask[ part[ k ] ] = false;
			}
		}
	}

	// Distance (texels) of every texel from the nearest one in the mask:
	// chamfer 1 / 1.414, forward and backward, twice round for the wrap
	static double[] Distance( bool[] mask, int w, int h )
	{
		int n = w * h;
		double[] d = new double[ n ];
		for ( int i = 0; i < n; i++ )
			d[ i ] = mask[ i ] ? 0.0 : 1e9;
		for ( int round = 0; round < 2; round++ )
		{
			for ( int y = 0; y < h; y++ )
			{
				for ( int x = 0; x < w; x++ )
				{
					int i = y * w + x, up = ( ( y - 1 + h ) % h ) * w;
					double v = d[ i ];
					v = Math.Min( v, d[ y * w + ( x - 1 + w ) % w ] + 1.0 );
					v = Math.Min( v, d[ up + x ] + 1.0 );
					v = Math.Min( v, d[ up + ( x - 1 + w ) % w ] + 1.414 );
					v = Math.Min( v, d[ up + ( x + 1 ) % w ] + 1.414 );
					d[ i ] = v;
				}
			}
			for ( int y = h - 1; y >= 0; y-- )
			{
				for ( int x = w - 1; x >= 0; x-- )
				{
					int i = y * w + x, down = ( ( y + 1 ) % h ) * w;
					double v = d[ i ];
					v = Math.Min( v, d[ y * w + ( x + 1 ) % w ] + 1.0 );
					v = Math.Min( v, d[ down + x ] + 1.0 );
					v = Math.Min( v, d[ down + ( x + 1 ) % w ] + 1.414 );
					v = Math.Min( v, d[ down + ( x - 1 + w ) % w ] + 1.414 );
					d[ i ] = v;
				}
			}
		}
		return d;
	}

	// A 24 bit .bmp: the colours (0..1) on the left, the heights (grey) on the right
	static byte[] SideBySide( float[] r, float[] g, float[] b, byte[] height, int w, int h )
	{
		int width = 2 * w, stride = ( width * 3 + 3 ) & ~3;
		byte[] bmp = new byte[ 54 + stride * h ];
		bmp[ 0 ] = (byte)'B';
		bmp[ 1 ] = (byte)'M';
		Array.Copy( BitConverter.GetBytes( bmp.Length ), 0, bmp, 2, 4 );
		Array.Copy( BitConverter.GetBytes( 54 ), 0, bmp, 10, 4 );
		Array.Copy( BitConverter.GetBytes( 40 ), 0, bmp, 14, 4 );
		Array.Copy( BitConverter.GetBytes( width ), 0, bmp, 18, 4 );
		Array.Copy( BitConverter.GetBytes( h ), 0, bmp, 22, 4 );
		Array.Copy( BitConverter.GetBytes( (short)1 ), 0, bmp, 26, 2 );
		Array.Copy( BitConverter.GetBytes( (short)24 ), 0, bmp, 28, 2 );
		Array.Copy( BitConverter.GetBytes( stride * h ), 0, bmp, 34, 4 );
		for ( int y = 0; y < h; y++ )
		{
			int row = 54 + ( h - 1 - y ) * stride;	// bottom-up
			for ( int x = 0; x < w; x++ )
			{
				int i = y * w + x, left = row + x * 3, right = row + ( w + x ) * 3;
				bmp[ left ] = (byte)Math.Min( 255, (int)( b[ i ] * 255 + 0.5 ) );
				bmp[ left + 1 ] = (byte)Math.Min( 255, (int)( g[ i ] * 255 + 0.5 ) );
				bmp[ left + 2 ] = (byte)Math.Min( 255, (int)( r[ i ] * 255 + 0.5 ) );
				bmp[ right ] = bmp[ right + 1 ] = bmp[ right + 2 ] = height[ i ];
			}
		}
		return bmp;
	}

	// An I8 .vtf (7.2, no thumbnail) with all mip levels, box filtered
	static byte[] WriteI8( byte[] level0, int w, int h )
	{
		int mips = 1;
		while ( ( w >> mips ) > 0 || ( h >> mips ) > 0 )
			mips++;
		byte[][] levels = new byte[ mips ][];
		levels[ 0 ] = level0;
		for ( int l = 1; l < mips; l++ )
		{
			int pw = Math.Max( w >> ( l - 1 ), 1 ), ph = Math.Max( h >> ( l - 1 ), 1 );
			int lw = Math.Max( w >> l, 1 ), lh = Math.Max( h >> l, 1 );
			byte[] prev = levels[ l - 1 ], cur = new byte[ lw * lh ];
			for ( int y = 0; y < lh; y++ )
			{
				for ( int x = 0; x < lw; x++ )
				{
					int x0 = Math.Min( x * 2, pw - 1 ), x1 = Math.Min( x * 2 + 1, pw - 1 );
					int y0 = Math.Min( y * 2, ph - 1 ), y1 = Math.Min( y * 2 + 1, ph - 1 );
					cur[ y * lw + x ] = (byte)( ( prev[ y0 * pw + x0 ] + prev[ y0 * pw + x1 ] + prev[ y1 * pw + x0 ] + prev[ y1 * pw + x1 ] + 2 ) / 4 );
				}
			}
			levels[ l ] = cur;
		}
		int total = 80;
		for ( int l = 0; l < mips; l++ )
			total += levels[ l ].Length;
		byte[] result = new byte[ total ];
		byte[] sig = { (byte)'V', (byte)'T', (byte)'F', 0 };
		Array.Copy( sig, result, 4 );
		Array.Copy( BitConverter.GetBytes( 7 ), 0, result, 4, 4 );
		Array.Copy( BitConverter.GetBytes( 2 ), 0, result, 8, 4 );
		Array.Copy( BitConverter.GetBytes( 80 ), 0, result, 12, 4 );
		Array.Copy( BitConverter.GetBytes( (ushort)w ), 0, result, 16, 2 );
		Array.Copy( BitConverter.GetBytes( (ushort)h ), 0, result, 18, 2 );
		Array.Copy( BitConverter.GetBytes( 0 ), 0, result, 20, 4 );			// flags
		Array.Copy( BitConverter.GetBytes( (ushort)1 ), 0, result, 24, 2 );	// frames
		Array.Copy( BitConverter.GetBytes( 0.5f ), 0, result, 32, 4 );		// reflectivity
		Array.Copy( BitConverter.GetBytes( 0.5f ), 0, result, 36, 4 );
		Array.Copy( BitConverter.GetBytes( 0.5f ), 0, result, 40, 4 );
		Array.Copy( BitConverter.GetBytes( 1.0f ), 0, result, 48, 4 );		// bump scale
		Array.Copy( BitConverter.GetBytes( 5 ), 0, result, 52, 4 );			// IMAGE_FORMAT_I8
		result[ 56 ] = (byte)mips;
		Array.Copy( BitConverter.GetBytes( -1 ), 0, result, 57, 4 );			// no thumbnail
		Array.Copy( BitConverter.GetBytes( (ushort)1 ), 0, result, 63, 2 );	// depth
		int at = 80;
		for ( int l = mips - 1; l >= 0; l-- )
		{
			Array.Copy( levels[ l ], 0, result, at, levels[ l ].Length );
			at += levels[ l ].Length;
		}
		return result;
	}

	// ------------------------------------------------------------------
	// Pictures of textures to look at (fidelity_report in the game, then
	// report.ps1): the largest level up to PICTURE_MAX texels a side as a
	// .png - the colours, and next to them the alpha when the format has one
	// (self-illumination and reflection masks live there); our height maps
	// (I8) in grey.
	// ------------------------------------------------------------------
	const int I8 = 5, IA88 = 6, A8 = 8;
	const int PICTURE_MAX = 1024;

	static long PictureSize( int w, int h, int format )
	{
		w = Math.Max( w, 1 );
		h = Math.Max( h, 1 );
		switch ( format )
		{
			case I8: case A8: return (long)w * h;
			case IA88: return (long)w * h * 2;
		}
		return ImageSize( w, h, format );
	}

	static bool FormatHasAlpha( int format )
	{
		switch ( format )
		{
			case DXT3: case DXT5: case DXT1_ONEBITALPHA: case RGBA8888: case ABGR8888: case ARGB8888: case BGRA8888: case IA88: case A8:
				return true;
		}
		return false;
	}

	// The .png of a texture, or null with the reason
	public static byte[] Picture( byte[] vtf, out string reason )
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
			reason = "header not understood";
			return null;
		}
		if ( ( flags & FLAG_ENVMAP ) != 0 || depth != 1 )
		{
			reason = "a cube map or volume texture";
			return null;
		}
		if ( width < 1 || height < 1 || mips < 1 || mips > 17 || PictureSize( width, height, format ) < 0 )
		{
			reason = "format " + format + " not handled";
			return null;
		}
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
		int level = 0;
		while ( level + 1 < mips && ( ( width >> level ) > PICTURE_MAX || ( height >> level ) > PICTURE_MAX ) )
			level++;
		int w = Math.Max( width >> level, 1 ), h = Math.Max( height >> level, 1 );
		long offset = start;
		for ( int l = mips - 1; l > level; l-- )
			offset += frames * PictureSize( width >> l, height >> l, format );
		if ( w > 4 * PICTURE_MAX || h > 4 * PICTURE_MAX || offset + PictureSize( w, h, format ) > vtf.Length )
		{
			reason = "cut off or too large";
			return null;
		}

		int n = w * h;
		float[] r = new float[ n ], g = new float[ n ], b = new float[ n ], a = new float[ n ];
		int o = (int)offset;
		if ( format == I8 || format == A8 || format == IA88 )
		{
			int step = format == IA88 ? 2 : 1;
			for ( int i = 0; i < n; i++ )
			{
				float v = vtf[ o + i * step ] / 255f;
				r[ i ] = g[ i ] = b[ i ] = format == A8 ? 1f : v;
				a[ i ] = format == IA88 ? vtf[ o + i * 2 + 1 ] / 255f : ( format == A8 ? v : 1f );
			}
		}
		else
		{
			Decode( vtf, o, w, h, format, r, g, b );
			if ( FormatHasAlpha( format ) )
				DecodeAlpha( vtf, o, w, h, format, a );
		}

		bool alpha = FormatHasAlpha( format );
		int pw = alpha ? 2 * w : w;
		byte[] rgb = new byte[ pw * h * 3 ];
		for ( int y = 0; y < h; y++ )
		{
			for ( int x = 0; x < w; x++ )
			{
				int i = y * w + x, p = ( y * pw + x ) * 3;
				rgb[ p ] = ToByte( r[ i ] );
				rgb[ p + 1 ] = ToByte( g[ i ] );
				rgb[ p + 2 ] = ToByte( b[ i ] );
				if ( alpha )
				{
					int q = ( y * pw + w + x ) * 3;
					rgb[ q ] = rgb[ q + 1 ] = rgb[ q + 2 ] = ToByte( a[ i ] );
				}
			}
		}
		return Png( rgb, pw, h );
	}

	static byte ToByte( float v )
	{
		return (byte)( Math.Max( 0f, Math.Min( 1f, v ) ) * 255f + 0.5f );
	}

	// An RGB .png (8 bit) with stored (uncompressed) deflate blocks: no
	// compression library needed, in Windows PowerShell 5.1 or PowerShell 7
	static byte[] Png( byte[] rgb, int w, int h )
	{
		int rowBytes = w * 3;
		byte[] raw = new byte[ h * ( rowBytes + 1 ) ];
		for ( int y = 0; y < h; y++ )
			Buffer.BlockCopy( rgb, y * rowBytes, raw, y * ( rowBytes + 1 ) + 1, rowBytes );	// filter 0
		System.IO.MemoryStream z = new System.IO.MemoryStream();
		z.WriteByte( 0x78 );
		z.WriteByte( 0x01 );
		int pos = 0;
		do
		{
			int len = Math.Min( 65535, raw.Length - pos );
			z.WriteByte( (byte)( pos + len >= raw.Length ? 1 : 0 ) );
			z.WriteByte( (byte)( len & 0xff ) );
			z.WriteByte( (byte)( len >> 8 ) );
			z.WriteByte( (byte)( ~len & 0xff ) );
			z.WriteByte( (byte)( ( ~len >> 8 ) & 0xff ) );
			z.Write( raw, pos, len );
			pos += len;
		} while ( pos < raw.Length );
		uint s1 = 1, s2 = 0;
		for ( int i = 0; i < raw.Length; i++ )
		{
			s1 = ( s1 + raw[ i ] ) % 65521;
			s2 = ( s2 + s1 ) % 65521;
		}
		WriteBigEndian( z, ( s2 << 16 ) | s1 );

		System.IO.MemoryStream png = new System.IO.MemoryStream();
		png.Write( new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }, 0, 8 );
		System.IO.MemoryStream header = new System.IO.MemoryStream();
		WriteBigEndian( header, (uint)w );
		WriteBigEndian( header, (uint)h );
		header.Write( new byte[] { 8, 2, 0, 0, 0 }, 0, 5 );	// 8 bit, RGB, deflate, no filter set, no interlace
		PngChunk( png, "IHDR", header.ToArray() );
		PngChunk( png, "IDAT", z.ToArray() );
		PngChunk( png, "IEND", new byte[ 0 ] );
		return png.ToArray();
	}

	static void WriteBigEndian( System.IO.Stream s, uint v )
	{
		s.WriteByte( (byte)( v >> 24 ) );
		s.WriteByte( (byte)( v >> 16 ) );
		s.WriteByte( (byte)( v >> 8 ) );
		s.WriteByte( (byte)v );
	}

	static uint[] s_Crc;

	static void PngChunk( System.IO.Stream s, string type, byte[] data )
	{
		if ( s_Crc == null )
		{
			uint[] table = new uint[ 256 ];
			for ( uint i = 0; i < 256; i++ )
			{
				uint c = i;
				for ( int k = 0; k < 8; k++ )
					c = ( c & 1 ) != 0 ? 0xEDB88320u ^ ( c >> 1 ) : c >> 1;
				table[ i ] = c;
			}
			s_Crc = table;
		}
		WriteBigEndian( s, (uint)data.Length );
		byte[] name = System.Text.Encoding.ASCII.GetBytes( type );
		s.Write( name, 0, 4 );
		s.Write( data, 0, data.Length );
		uint crc = 0xFFFFFFFFu;
		for ( int i = 0; i < 4; i++ )
			crc = s_Crc[ ( crc ^ name[ i ] ) & 0xff ] ^ ( crc >> 8 );
		for ( int i = 0; i < data.Length; i++ )
			crc = s_Crc[ ( crc ^ data[ i ] ) & 0xff ] ^ ( crc >> 8 );
		WriteBigEndian( s, crc ^ 0xFFFFFFFFu );
	}
}
