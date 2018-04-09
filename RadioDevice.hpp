#ifndef RADIODEVICE_HPP
#define RADIODEVICE_HPP

#include <lang/String.hpp>
#include <lang/System.hpp>

#include <uhd/usrp/multi_usrp.hpp>

#define DEFAULT_RX_SPS      1
#define DEFAULT_TX_SPS      4
#define UHD_RESTART_TIMEOUT     1.0

#define MHz(f) ((f)/1e6)

enum class DeviceType {
	Undef,
	USRP1,
	USRP2,
	B100,
	B200,
	B210,
	E1xx,
	E3xx,
	X3xx,
	UMTRX,
	LIME_USB,
	LIME_PCIE,
};


class SampleBuffer : extends Object {
private:
	short *buf;
	int capacity;
	int idx,len;
	double rate; //allows to convert between time in ticks and real time(in s)
	jlong tm0;   //timestamp of sample first sample in buffer (counted in ticks, 1sample=1tick)

public:
	SampleBuffer(int capacity, double rate) : capacity(capacity), idx(0), len(0), rate(rate) {
		buf = new short[capacity];
		tm0 = 0; // or LONG_MIN
	}
	virtual ~SampleBuffer() {
		delete[] buf;
	}

	String toString() const {
		return String::format("cap=%d,idx=%d,len=%d,tm0=%ld",capacity,idx,len,tm0);
	}

	int available(jlong t) const;
	int write(short *b, int l, jlong t);
	int read(short *b, int l, jlong t);
};

class RadioDevice : extends Object {
private:
	uhd::usrp::multi_usrp::sptr usrp_dev;
	uhd::rx_streamer::sptr rx_stream;
	uhd::tx_streamer::sptr tx_stream;

	DeviceType devType = DeviceType::Undef;
	int chans = 0; // number of channels
	int rx_sps = 0, tx_sps = 0; //samplaes per symbol(1..4)
	double rx_rate = 0, tx_rate = 0;
	double master_clock_offset = 0;
	long rx_pkt_cnt = 0, tx_pkt_cnt = 0;
	jlong ts_offs = 0;

	Array<double> rx_gain, tx_gain; //[chans]
	Array<double> rx_freq, tx_freq; //[chans]

public:
	RadioDevice(int rx_sps=DEFAULT_RX_SPS, int tx_sps=DEFAULT_TX_SPS);
	~RadioDevice();
	String toString() const;

	boolean open(const String& args);
	void close();
	void restart(); //start receiving

	boolean setAntenna(const String& rx, const String& tx);
	boolean setFreq(double freq, int chan, bool tx);

	void rx_flush(int pkts);
	void recv();
};


#endif
