#include <lang/System.hpp>
#include <lang/Math.hpp>

#include "MobileStation.hpp"

/*
GSM Timing Table
  	            Symbol 	Bursts 	 Frames 	Time 					Rate
Symbol 	             1 	   1/8	 1/1250 	48/13 μs (3.692 μs) 	270833/s
Burst 	        156.25 	     1 	 1/8		15/26 ms (576.9 μs) 	1733/s
Frame 	          1250 	     8 	 1			60/13 ms (4.615 ms) 	216.6/s
26 Multiframe 	 32500 	   208 	 26 		120 ms 					8.333/s
51 Multiframe 	 63750 	   408 	 51 		235.4 ms 				4.248/s
52 Multiframe 	 65000 	   416 	 52 		240 ms 					4.167/s
Superframe 	2071875000 	 84864 	 1326		6120 ms 				9.803/min
Hyperframe 	4.2432E+12 173801472 2715648 	3h:28m:53.760s			6.893/day 

1 Frame = 8 Bursts = 1250 Symbols
1 Burst =~ 148 bits of information


5 Burst types: Normal(both), FREQ Correction(dnlink), Synchro(dnlink), Dummy(dnlink), Access(uplink)

Normal burts
bits
3	tail	give time to ramp transmitter power
57	data	information
1	flag
26	train
1	flag
57	data
3	tail
8.25 guard

Access burst
7	tail
41	train
36	data
3	tail
69.25 guard

http://www.teletopix.org/gsm/what-is-burst-in-gsm-and-burst-types-in-gsm/

*/

/*
uplink - Mobile to Base
dnlink - Base to Mobile

Band    n=ARFCN     f(ul)                   f(dl)
=======================================================
GSM850  128..251    890.0 + 0.2*n           f(ul)+45.0
GSM900  0..124      890.0 + 0.2*n           f(ul)+45.0
GSM900  955..1023
GSM1800 512..885    1720.2 + 0.2*(n-512)    f(ul)+95.0
GSM1900 512..810    1850.2 + 0.2*(n-512)    f(ul)+80.0
=======================================================
Dynamic x=ARFCS_FIRST, y=BAND_OFFSET, z=ARFCN_RANGE
GSM750              777.2 + 0.2*(n-x+y)     f(ul)-30.0

*/

// https://en.wikipedia.org/wiki/Absolute_radio-frequency_channel_numbe
// http://www.rfwireless-world.com/Terminology/GSM-ARFCN-to-frequency-conversion.html
// http://www.rfwireless-world.com/Tutorials/gsm-frame-structure.html
// http://www.sharetechnote.com/html/Handbook_GSM_Band_ARFCN_Frequency.html (dynamic also)
// http://www.telecomabc.com/a/arfcn.html
namespace {
struct {
	GsmBand band;
	int first, last;
	double base_freq;
	double dnl_offs;
} band_channels[] = {
	{GsmBand::GSM450,  259,  293,  450.6-259*0.2, 10.0},
	{GsmBand::GSM480,  306,  340,  479.0-306*0.2, 10.0},
	{GsmBand::GSM750,  438,  511,  747.2-438*0.2, 30.0},
	{GsmBand::GSM850,  128,  251,  824.2-128*0.2, 45.0},

	{GsmBand::GSM900,    0,  124,          890.0, 45.0}, //primary-gsm/extemded-gsm
	{GsmBand::GSM900,  955, 1023, 890.0-1024*0.2, 45.0}, //gsm-rail/extemded-gsm
	{GsmBand::GSM1800, 512,  885, 1710.2-512*0.2, 95.0}, //dcs
	{GsmBand::GSM1900, 512,  810, 1850.2-512*0.2, 80.0}, //pcs

	{GsmBand::Undef, 0, 0, 0, 0}
};
}

GsmBand upLinkFreq(GsmBand band, int n, double& freq) {
	for (int i = 0; band_channels[i].band != GsmBand::Undef; ++i) {
		if (n < band_channels[i].first || n > band_channels[i].last) continue;
		freq = band_channels[i].base_freq + 0.2*n;
		return band_channels[i].band;
	}
	return GsmBand::Undef;
}
GsmBand dnLinkFreq(GsmBand band, int n, double& freq) {
	for (int i = 0; band_channels[i].band != GsmBand::Undef; ++i) {
		if (band != GsmBand::Undef && band != band_channels[i].band) continue;
		if (n < band_channels[i].first || n > band_channels[i].last) continue;
		freq = band_channels[i].base_freq + 0.2*n + band_channels[i].dnl_offs;
		return band_channels[i].band;
	}
	return GsmBand::Undef;
}

MobileStation::MobileStation() {
}
MobileStation::~MobileStation() {
	stop();
}

void MobileStation::btsScan(GsmBand band) {
	LOGD("btsScan %d...", band);
	//int decimation = (int)(master_clock_freq / GSM_RATE);

	int chan = 0;
	for (int i = 0; band_channels[i].band != GsmBand::Undef; ++i) {
		if (band != band_channels[i].band) continue;
		for (int n = band_channels[i].first; n <= band_channels[i].last; ++n) {
			double upl = band_channels[i].base_freq + 0.2*n;
			double dnl = upl + band_channels[i].dnl_offs; 
			LOGD("ARFCN = %d,  upl %.2lf, dnl %.2lf", n, upl, dnl);
			upl *= 1e6;
			dnl *= 1e6;

			boolean done = false;
			// tune radio to downlink (Base-to-Mobile)
			usrp.setFreq(dnl, chan, false);
			while (!done) {
				usrp.recv();
				done = true;
			}
		}
	}
	
}

void MobileStation::start() {
	String addr = "";  // default device (autodetect)
	if (!usrp.open(addr)) return ;
	btsScan(GsmBand::GSM1800);
}
void MobileStation::stop() {
	LOGD("MobileStation::stop");
	usrp.close();
}

String MobileStation::toString() const {
	return String::format("MobileStation on %s", usrp.toString().cstr());
}
