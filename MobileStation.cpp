#include <lang/System.hpp>
#include <lang/Math.hpp>

#include "MobileStation.hpp"

MobileStation::MobileStation() {
}
MobileStation::~MobileStation() {
	stop();
}

/*
Band    n=ARFCN     f(ul)                   f(dl)
=======================================================
GSM850  128..251    890.0 + 0.2*n           f(ul)+45.0
GSM900  0..124      890.0 + 0.2*n           f(ul)+45.0
GSM900  955..1023
GSM1800 512..885    1720.2 + 0.2*(n-512)    f(ul)+95.0
GSM1900 512..810    1850.2 + 0.2*(n-512)    f(ul)+80.0
*/
// http://www.rfwireless-world.com/Terminology/GSM-ARFCN-to-frequency-conversion.html
// http://www.rfwireless-world.com/Tutorials/gsm-frame-structure.html
struct {
	GsmBand band;
	int first, last;
	double base_freq;
	double dnl_offs;
} band_channels[] = {
	{GsmBand::GSM450,  259,  293,  450.6-259*0.2, 10.0},
	{GsmBand::GSM480,  306,  340,  479.0-306*0.2, 10.0},
	{GsmBand::GSM750,  438,  511,  747.2-438*0.2, 30.0},
	{GsmBand::GSM850,  128,  251,  824.2-128*0.2, 30.0},

	{GsmBand::GSM900,    0,  124,  890.0, 45.0}, //p-gsm
	{GsmBand::GSM900,  955, 1023,  890.0-1024*0.2, 45.0}, //gsm-r(e-gsm)
	{GsmBand::GSM1800, 512,  885, 1710.2-512*0.2, 95.0}, //dcs
	{GsmBand::GSM1900, 512,  810, 1850.2-512*0.2, 80.0}, //pcs

	{GsmBand::Undef, 0, 0, 0, 0}
};

GsmBand upLinkFreq(int n, double& freq) {
	for (int i = 0; band_channels[i].band != GsmBand::Undef; ++i) {
		if (n < band_channels[i].first || n > band_channels[i].last) continue;
		freq = band_channels[i].base_freq + 0.2*n;
		return band_channels[i].band;
	}
	return GsmBand::Undef;
}
GsmBand dnLinkFreq(int n, double& freq) {
	for (int i = 0; band_channels[i].band != GsmBand::Undef; ++i) {
		if (n < band_channels[i].first || n > band_channels[i].last) continue;
		freq = band_channels[i].base_freq + 0.2*n + band_channels[i].dnl_offs;
		return band_channels[i].band;
	}
	return GsmBand::Undef;
}

void MobileStation::btsScan(GsmBand band) {
	LOGD("btsScan...");
	//int decimation = (int)(master_clock_freq / GSM_RATE);

	int chan = 0;
	for (int i = 0; band_channels[i].band != GsmBand::Undef; ++i) {
		if (band != band_channels[i].band) continue;
		for (int n = band_channels[i].first; n <= band_channels[i].last; ++n) {
			double upl = band_channels[i].base_freq + 0.2*n;
			double dnl = upl + band_channels[i].dnl_offs; 
			LOGD("ARFCN = %d,  upl %.2lf, dnl %.2lf", n, upl, dnl);
			upl *= 1e6;

			boolean done = false;
			// tune radio to upl
			usrp.setFreq(upl, chan, false);
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
	btsScan(GsmBand::GSM900);
}
void MobileStation::stop() {
	LOGD("MobileStation::stop");
	usrp.close();
}

String MobileStation::toString() const {
	return String::format("MobileStation on %s", usrp.toString().cstr());
}
