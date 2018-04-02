#ifndef MOBILESTATION_HPP
#define MOBILESTATION_HPP

#include "RadioDevice.hpp"

enum class GsmBand {
	Undef,
	GSM450,
	GSM480,
	GSM750,
	GSM850,

	GSM900,
	GSM1800,
	GSM1900,
};

class MobileStation : extends Object {
private:
	RadioDevice usrp;
	int arfcn; // Absolute radio-frequency channel number

public:
	MobileStation();
	~MobileStation();
	String toString() const;

	void start();
	void stop();
	void btsScan(GsmBand band);
};


#endif
