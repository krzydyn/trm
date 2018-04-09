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


template<class T>
class SampleBuffer : extends Object {
private:
	T *buf;
	int capacity;
	int idx,len;
	double rate;
	jlong tm0; //timestamp of sample 0
public:
	SampleBuffer(int capacity, double rate) : capacity(capacity), idx(0), len(0), rate(rate) {
		buf = new T[capacity];
		tm0 = 0; // or LONG_MIN
	}
	virtual ~SampleBuffer() {
		delete[] buf;
	}

	String toString() const {
		return String::format("cap=%d,idx=%d,len=%d,tm0=%ld",capacity,idx,len,tm0);
	}

	int available(jlong t) const {
		if (t < tm0) return -1; // past
		jlong tm1 = tm0 + len;
		if (t >= tm1) return 0; // future
		return (int)(tm1 - t);  // number of samples
	}

	int write(T *b, int l, jlong t) {
		if (l < 0 || l > capacity) throw RuntimeException("wrong length "+l);
		if (l == 0) return 0;
		if (len == 0) tm0 = t;
		if (t < tm0) {
			LOGW("Attempt to write data in the past");
			return -1;
		}
		jlong tm1 = tm0 + len;
		if (t < tm1) {
			LOGW("Overwriting old data");
		}
		if (tm1 < t) {
			LOGW("Making Gap in data");
			int l1 = (int)(t-tm1);
			int i1=(idx+l1)%capacity;
			if (i1+l1 <= capacity) memset(buf+i1, 0, l1*sizeof(T));
			else {
				int rem = capacity-i1;
				memset(buf + i1, 0, rem*sizeof(T));
				memset(buf, 0, (l1-rem)*sizeof(T));
			}
			len += l1;
		}

		int i0=(idx+(int)(t-tm0))%capacity;
		if (i0+l <= capacity) {
			memcpy(buf+i0, b, l*sizeof(T));
		}
		else {
			int rem = capacity-i0;
			memcpy(buf + i0, b, rem*sizeof(T));
			memcpy(buf, b + rem, (l-rem)*sizeof(T));
		}
		len += l;
		if (len > capacity) {
			idx = (i0+len+capacity)%capacity;
			len = capacity;
			tm0 = t-capacity;
			LOGW("Buffer overflow");
		}
		return l;
	}

	// read samples starting from time=t
	int read(T *b, int l, jlong t) {
		if (l <= 0) throw RuntimeException("wrong length "+l);
		if (t < tm0) return -1; // past data
		jlong tm1 = tm0 + len;
		if (t >= tm1) return 0; //future data

		int n = (int)(t-tm1); //numbe of samples from t to end
		if (l > n) l = n;

		int i0=(idx+(int)(t-tm0))%capacity;
		

		if (i0+l <= capacity) {
			memcpy(b, buf+i0, l*sizeof(T));
		}
		else {
			int rem = capacity-i0;
			memcpy(b, buf + i0, rem*sizeof(T));
			memcpy(b + rem, buf, (l-rem)*sizeof(T));
		}
		idx = (i0 + l)%capacity;
		tm0 = t + l;
		len -= l;
		return l;
	}
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
