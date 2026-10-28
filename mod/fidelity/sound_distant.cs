// mapbase-fidelity: far-away versions of gunshots and explosions, for deploy.ps1 (loaded with Add-Type).
//
// A shot heard from far away has lost its highs (air and walls swallow them),
// and its echo off buildings comes after it. From the game's own sound (plain
// PCM .wav, 8 or 16 bit, mono or stereo) this makes a mono copy:
//
//   - high pass 80 Hz, two low passes at 1.1 kHz (12 dB per octave each)
//   - early echoes off walls: 90, 190, 330, 520 and 800 ms, each weaker
//   - a diffuse tail (Schroeder: four combs, two all-passes), 1.8 s longer
//   - the loudest point at 0.7 (about -3 dB), 16 bit
//
// No generative anything: classic filters and delays on the game's sound.
// client.dll (fidelity_effects.cpp) plays it for shots far away, delayed by
// the time sound takes to get there.
//
// C# 5 (Windows PowerShell 5.1 compiles with the .NET Framework compiler).

using System;
using System.Collections.Generic;

public static class FidelitySoundDistant
{
	static int Int32At( byte[] b, int o ) { return b[ o ] | ( b[ o + 1 ] << 8 ) | ( b[ o + 2 ] << 16 ) | ( b[ o + 3 ] << 24 ); }
	static int Int16At( byte[] b, int o ) { return b[ o ] | ( b[ o + 1 ] << 8 ); }

	// RBJ biquad, low or high pass, Q 0.707
	sealed class Pass
	{
		double b0, b1, b2, a1, a2, x1, x2, y1, y2;

		public Pass( double rate, double frequency, bool bLow )
		{
			double w = 2.0 * Math.PI * Math.Min( frequency, rate * 0.45 ) / rate;
			double cos = Math.Cos( w ), alpha = Math.Sin( w ) / ( 2.0 * 0.7071 );
			double a0 = 1.0 + alpha;
			if ( bLow )
			{
				b0 = ( 1.0 - cos ) / 2.0; b1 = 1.0 - cos; b2 = ( 1.0 - cos ) / 2.0;
			}
			else
			{
				b0 = ( 1.0 + cos ) / 2.0; b1 = -( 1.0 + cos ); b2 = ( 1.0 + cos ) / 2.0;
			}
			a1 = -2.0 * cos; a2 = 1.0 - alpha;
			b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
		}

		public double Run( double x )
		{
			double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
			x2 = x1; x1 = x; y2 = y1; y1 = y;
			return y;
		}
	}

	// A far-away copy of the .wav (mono, 16 bit, same rate), or null with the reason
	public static byte[] Make( byte[] wav, out string reason )
	{
		reason = null;
		if ( wav == null || wav.Length < 12 || wav[ 0 ] != 'R' || wav[ 1 ] != 'I' || wav[ 2 ] != 'F' || wav[ 3 ] != 'F' ||
			wav[ 8 ] != 'W' || wav[ 9 ] != 'A' || wav[ 10 ] != 'V' || wav[ 11 ] != 'E' )
		{
			reason = "not a .wav";
			return null;
		}
		int fmtAt = -1, dataAt = -1, dataSize = 0;
		int at = 12;
		while ( at + 8 <= wav.Length )
		{
			int size = Int32At( wav, at + 4 );
			if ( size < 0 || at + 8 + (long)size > wav.Length )
				size = wav.Length - at - 8;
			string id = new string( new char[] { (char)wav[ at ], (char)wav[ at + 1 ], (char)wav[ at + 2 ], (char)wav[ at + 3 ] } );
			if ( id == "fmt " ) fmtAt = at;
			else if ( id == "data" ) { dataAt = at; dataSize = size; }
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
		if ( format != 1 || ( bits != 8 && bits != 16 ) || channels < 1 || channels > 2 || rate < 8000 || rate > 96000 )
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

		// mono, filtered
		Pass high = new Pass( rate, 80.0, false ), low1 = new Pass( rate, 1100.0, true ), low2 = new Pass( rate, 1100.0, true );
		int tail = (int)( rate * 1.8 );
		int total = frames + tail;
		double[] dry = new double[ total ];
		for ( int i = 0; i < total; i++ )
		{
			double v = 0.0;
			if ( i < frames )
			{
				for ( int c = 0; c < channels; c++ )
				{
					int p = dataAt + 8 + ( i * channels + c ) * bytesPerSample;
					v += bits == 16 ? (short)Int16At( wav, p ) / 32768.0 : ( wav[ p ] - 128 ) / 128.0;
				}
				v /= channels;
			}
			dry[ i ] = low2.Run( low1.Run( high.Run( v ) ) );
		}

		// early echoes off walls
		double[] outSamples = new double[ total ];
		double[] tapSeconds = { 0.09, 0.19, 0.33, 0.52, 0.8 };
		double[] tapGains = { 0.45, 0.32, 0.22, 0.14, 0.08 };
		for ( int i = 0; i < total; i++ )
		{
			double v = dry[ i ] * 0.8;
			for ( int t = 0; t < tapSeconds.Length; t++ )
			{
				int j = i - (int)( tapSeconds[ t ] * rate );
				if ( j >= 0 )
					v += dry[ j ] * tapGains[ t ];
			}
			outSamples[ i ] = v;
		}

		// diffuse tail: four combs into two all-passes
		double[] combSeconds = { 0.0297, 0.0371, 0.0411, 0.0437 };
		double[] combState = new double[ total ];
		for ( int k = 0; k < combSeconds.Length; k++ )
		{
			int d = Math.Max( 1, (int)( combSeconds[ k ] * rate ) );
			double[] line = new double[ total ];
			for ( int i = 0; i < total; i++ )
			{
				double back = i >= d ? line[ i - d ] : 0.0;
				line[ i ] = dry[ i ] + back * 0.86;
				combState[ i ] += back;
			}
		}
		double[] allSeconds = { 0.005, 0.0017 };
		for ( int k = 0; k < allSeconds.Length; k++ )
		{
			int d = Math.Max( 1, (int)( allSeconds[ k ] * rate ) );
			double[] y = new double[ total ];
			for ( int i = 0; i < total; i++ )
			{
				double xd = i >= d ? combState[ i - d ] : 0.0;
				double yd = i >= d ? y[ i - d ] : 0.0;
				y[ i ] = -0.7 * combState[ i ] + xd + 0.7 * yd;
			}
			combState = y;
		}
		double peak = 0.0;
		for ( int i = 0; i < total; i++ )
		{
			outSamples[ i ] += combState[ i ] * 0.2;
			peak = Math.Max( peak, Math.Abs( outSamples[ i ] ) );
		}
		if ( peak < 1e-5 )
		{
			reason = "silent";
			return null;
		}
		double gain = 0.7 / peak;

		// RIFF: fmt (PCM, mono, 16 bit) + data
		var output = new List<byte>( 44 + total * 2 );
		output.AddRange( new byte[] { (byte)'R', (byte)'I', (byte)'F', (byte)'F' } );
		output.AddRange( BitConverter.GetBytes( 36 + total * 2 ) );
		output.AddRange( new byte[] { (byte)'W', (byte)'A', (byte)'V', (byte)'E', (byte)'f', (byte)'m', (byte)'t', (byte)' ' } );
		output.AddRange( BitConverter.GetBytes( 16 ) );
		output.AddRange( BitConverter.GetBytes( (short)1 ) );
		output.AddRange( BitConverter.GetBytes( (short)1 ) );
		output.AddRange( BitConverter.GetBytes( rate ) );
		output.AddRange( BitConverter.GetBytes( rate * 2 ) );
		output.AddRange( BitConverter.GetBytes( (short)2 ) );
		output.AddRange( BitConverter.GetBytes( (short)16 ) );
		output.AddRange( new byte[] { (byte)'d', (byte)'a', (byte)'t', (byte)'a' } );
		output.AddRange( BitConverter.GetBytes( total * 2 ) );
		// the end fades out over the last 0.3 s (no click)
		int fade = Math.Min( total, (int)( rate * 0.3 ) );
		for ( int i = 0; i < total; i++ )
		{
			double v = outSamples[ i ] * gain;
			if ( i >= total - fade )
				v *= ( total - i ) / (double)fade;
			short s16 = (short)Math.Round( Math.Max( -1.0, Math.Min( 1.0, v ) ) * 32767.0 );
			output.AddRange( BitConverter.GetBytes( s16 ) );
		}
		return output.ToArray();
	}
}
