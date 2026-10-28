// mapbase-fidelity: makes game sounds fuller, for sound_tool.ps1 (loaded with Add-Type).
//
// Works on .wav files with plain PCM (8 or 16 bit, mono or stereo) - what
// Half-Life 2's weapon and most other sounds are. The other chunks of the
// file (cue points, loops, the "smpl" chunk) are kept as they are.
//
//   bass    low shelf at 120 Hz, in dB (punch, "beefier")
//   bite    high shelf at 3.5 kHz, in dB (crack, presence)
//   drive   soft saturation 0..1 (thicker, a little grit; tanh)
//   pitch   in semitones, negative = deeper (the sound gets longer). Not for
//           sounds with cue points or loops: their positions would be wrong.
//
// The loudest point ends up as loud as before (a little louder at most,
// never clipping), so the mix of the game stays: the sound gets fuller, not
// just louder. Every offset is checked: a broken file is reported.
//
// C# 5 (Windows PowerShell 5.1 compiles with the .NET Framework compiler).

using System;
using System.Collections.Generic;

public static class FidelitySoundBeef
{
	static int Int32At( byte[] b, int o ) { return b[ o ] | ( b[ o + 1 ] << 8 ) | ( b[ o + 2 ] << 16 ) | ( b[ o + 3 ] << 24 ); }
	static int Int16At( byte[] b, int o ) { return b[ o ] | ( b[ o + 1 ] << 8 ); }

	// RBJ biquad shelf (Audio EQ Cookbook), slope 1
	sealed class Shelf
	{
		double b0, b1, b2, a1, a2, x1, x2, y1, y2;

		public Shelf( double rate, double frequency, double gainDb, bool bLow )
		{
			double A = Math.Pow( 10.0, gainDb / 40.0 );
			double w = 2.0 * Math.PI * frequency / rate;
			double cos = Math.Cos( w ), sin = Math.Sin( w );
			double alpha = sin / 2.0 * Math.Sqrt( 2.0 );
			double s = 2.0 * Math.Sqrt( A ) * alpha;
			double a0;
			if ( bLow )
			{
				b0 = A * ( ( A + 1 ) - ( A - 1 ) * cos + s );
				b1 = 2 * A * ( ( A - 1 ) - ( A + 1 ) * cos );
				b2 = A * ( ( A + 1 ) - ( A - 1 ) * cos - s );
				a0 = ( A + 1 ) + ( A - 1 ) * cos + s;
				a1 = -2 * ( ( A - 1 ) + ( A + 1 ) * cos );
				a2 = ( A + 1 ) + ( A - 1 ) * cos - s;
			}
			else
			{
				b0 = A * ( ( A + 1 ) + ( A - 1 ) * cos + s );
				b1 = -2 * A * ( ( A - 1 ) + ( A + 1 ) * cos );
				b2 = A * ( ( A + 1 ) + ( A - 1 ) * cos - s );
				a0 = ( A + 1 ) - ( A - 1 ) * cos + s;
				a1 = 2 * ( ( A - 1 ) - ( A + 1 ) * cos );
				a2 = ( A + 1 ) - ( A - 1 ) * cos - s;
			}
			b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
		}

		public double Run( double x )
		{
			double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
			x2 = x1; x1 = x; y2 = y1; y1 = y;
			return y;
		}
	}

	// A copy of the .wav made fuller, or null with the reason
	public static byte[] Process( byte[] wav, double bass, double bite, double drive, double pitch, out string reason )
	{
		reason = null;
		if ( wav == null || wav.Length < 12 || wav[ 0 ] != 'R' || wav[ 1 ] != 'I' || wav[ 2 ] != 'F' || wav[ 3 ] != 'F' ||
			wav[ 8 ] != 'W' || wav[ 9 ] != 'A' || wav[ 10 ] != 'V' || wav[ 11 ] != 'E' )
		{
			reason = "not a .wav";
			return null;
		}

		// The chunks: fmt, data, the rest kept in order
		int fmtAt = -1, dataAt = -1, dataSize = 0;
		bool bCuesOrLoops = false;
		var chunks = new List<int[]>();	// offset of the chunk header, size
		int at = 12;
		while ( at + 8 <= wav.Length )
		{
			int size = Int32At( wav, at + 4 );
			if ( size < 0 || at + 8 + (long)size > wav.Length )
			{
				// MP3/ADPCM files and some tools write a wrong last size: take what is there
				size = wav.Length - at - 8;
			}
			string id = new string( new char[] { (char)wav[ at ], (char)wav[ at + 1 ], (char)wav[ at + 2 ], (char)wav[ at + 3 ] } );
			if ( id == "fmt " ) fmtAt = at;
			else if ( id == "data" ) { dataAt = at; dataSize = size; }
			else if ( id == "cue " || id == "smpl" ) bCuesOrLoops = true;
			chunks.Add( new int[] { at, size } );
			at += 8 + size + ( size & 1 );
		}
		if ( fmtAt < 0 || dataAt < 0 || Int32At( wav, fmtAt + 4 ) < 16 )
		{
			reason = "no format or data";
			return null;
		}
		int format = Int16At( wav, fmtAt + 8 );
		int channels = Int16At( wav, fmtAt + 10 );
		int rate = Int32At( wav, fmtAt + 12 );
		int bits = Int16At( wav, fmtAt + 22 );
		if ( format != 1 || ( bits != 8 && bits != 16 ) || channels < 1 || channels > 2 || rate < 4000 || rate > 192000 )
		{
			reason = format != 1 ? "compressed (format " + format + ")" : "not 8/16 bit mono/stereo";
			return null;
		}
		int bytesPerSample = bits / 8;
		int frames = dataSize / ( bytesPerSample * channels );
		if ( frames < 16 )
		{
			reason = "too short";
			return null;
		}

		// To -1..1
		double[][] samples = new double[ channels ][];
		double peakBefore = 0.0;
		for ( int c = 0; c < channels; c++ )
		{
			samples[ c ] = new double[ frames ];
			for ( int i = 0; i < frames; i++ )
			{
				int p = dataAt + 8 + ( i * channels + c ) * bytesPerSample;
				double v = bits == 16 ? (short)Int16At( wav, p ) / 32768.0 : ( wav[ p ] - 128 ) / 128.0;
				samples[ c ][ i ] = v;
				peakBefore = Math.Max( peakBefore, Math.Abs( v ) );
			}
		}
		if ( peakBefore < 1e-4 )
		{
			reason = "silent";
			return null;
		}

		// Bass, bite, drive
		double driveGain = 1.0 + 4.0 * drive;
		double driveNorm = Math.Tanh( driveGain );
		for ( int c = 0; c < channels; c++ )
		{
			Shelf low = new Shelf( rate, 120.0, bass, true );
			Shelf high = new Shelf( rate, Math.Min( 3500.0, rate * 0.4 ), bite, false );
			double[] s = samples[ c ];
			for ( int i = 0; i < frames; i++ )
			{
				double v = high.Run( low.Run( s[ i ] ) );
				if ( drive > 0.0 )
					v = v * ( 1.0 - drive ) + Math.Tanh( v * driveGain ) / driveNorm * drive;
				s[ i ] = v;
			}
		}

		// Pitch: played slower (resampled, linear), not for cues and loops
		if ( Math.Abs( pitch ) > 0.01 && !bCuesOrLoops )
		{
			double step = Math.Pow( 2.0, pitch / 12.0 );
			int newFrames = (int)Math.Min( frames / step, 64.0 * 1024 * 1024 );
			for ( int c = 0; c < channels; c++ )
			{
				double[] s = samples[ c ], r = new double[ newFrames ];
				for ( int i = 0; i < newFrames; i++ )
				{
					double pos = i * step;
					int i0 = Math.Min( (int)pos, frames - 1 ), i1 = Math.Min( i0 + 1, frames - 1 );
					double f = pos - i0;
					r[ i ] = s[ i0 ] * ( 1.0 - f ) + s[ i1 ] * f;
				}
				samples[ c ] = r;
			}
			frames = newFrames;
		}

		// As loud at its loudest point as before (a tenth louder at most), never clipping
		double peakAfter = 0.0;
		for ( int c = 0; c < channels; c++ )
			for ( int i = 0; i < frames; i++ )
				peakAfter = Math.Max( peakAfter, Math.Abs( samples[ c ][ i ] ) );
		double gain = Math.Min( peakBefore * 1.1, 0.98 ) / Math.Max( peakAfter, 1e-6 );

		// The new file: the chunks as before, with the new data
		int newDataSize = frames * channels * bytesPerSample;
		var output = new List<byte>( wav.Length + newDataSize );
		output.AddRange( new byte[] { (byte)'R', (byte)'I', (byte)'F', (byte)'F', 0, 0, 0, 0, (byte)'W', (byte)'A', (byte)'V', (byte)'E' } );
		foreach ( int[] chunk in chunks )
		{
			if ( chunk[ 0 ] == dataAt )
			{
				output.AddRange( new byte[] { (byte)'d', (byte)'a', (byte)'t', (byte)'a' } );
				output.AddRange( BitConverter.GetBytes( newDataSize ) );
				for ( int i = 0; i < frames; i++ )
				{
					for ( int c = 0; c < channels; c++ )
					{
						double v = Math.Max( -1.0, Math.Min( 1.0, samples[ c ][ i ] * gain ) );
						if ( bits == 16 )
							output.AddRange( BitConverter.GetBytes( (short)Math.Round( v * 32767.0 ) ) );
						else
							output.Add( (byte)Math.Round( v * 127.0 + 128.0 ) );
					}
				}
				if ( ( newDataSize & 1 ) != 0 )
					output.Add( 0 );
			}
			else
			{
				int length = 8 + chunk[ 1 ];
				for ( int i = 0; i < length; i++ )
					output.Add( wav[ chunk[ 0 ] + i ] );
				if ( ( chunk[ 1 ] & 1 ) != 0 && chunk[ 0 ] + length < wav.Length )
					output.Add( wav[ chunk[ 0 ] + length ] );
			}
		}
		byte[] result = output.ToArray();
		byte[] riffSize = BitConverter.GetBytes( result.Length - 8 );
		Array.Copy( riffSize, 0, result, 4, 4 );
		return result;
	}
}
