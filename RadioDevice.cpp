#include <lang/System.hpp>
#include <lang/Math.hpp>

#include "RadioDevice.hpp"


#define GSMRATE (1625000.0 / 6.0)
#define SAMPLE_BUF_SZ   (1 << 20)

namespace {
struct {
	DeviceType type;
	int tx_sps, rx_sps;
	double offs;
} uhd_offsets[] = {
	//{ DeviceType::USRP1, 1, 1,       0.0},
	//{ DeviceType::USRP1, 4, 1,       0.0},
	{ DeviceType::USRP2, 1, 1, 1.2184e-4},
	{ DeviceType::USRP2, 4, 1, 8.0230e-5},
	{ DeviceType::B100,  1, 1, 1.2104e-4},
	{ DeviceType::B100,  4, 1, 7.9307e-5},
	//{ DeviceType::B200,  1, 1, B2XX_TIMING_1SPS},
	//{ DeviceType::B200,  4, 1, B2XX_TIMING_4SPS},
	//{ DeviceType::B210,  1, 1, B2XX_TIMING_1SPS},
	//{ DeviceType::B210,  4, 1, B2XX_TIMING_4SPS},
	{ DeviceType::E1xx,  1, 1, 9.5192e-5},
	{ DeviceType::E1xx,  4, 1, 6.5571e-5},
	{ DeviceType::E3xx,  1, 1, 1.84616e-4},
	{ DeviceType::E3xx,  4, 1, 1.29231e-4},
	{ DeviceType::X3xx,  1, 1, 1.5360e-4},
	{ DeviceType::X3xx,  4, 1, 1.1264e-4},
	{ DeviceType::UMTRX, 1, 1, 9.9692e-5},
	{ DeviceType::UMTRX, 4, 1, 7.3846e-5},
	//{ DeviceType::B200,  4, 4, B2XX_TIMING_4_4SPS},
	//{ DeviceType::B210,  4, 4, B2XX_TIMING_4_4SPS},
	{ DeviceType::UMTRX, 4, 4, 5.1503e-5},
	{ DeviceType::LIME_USB, 4, 4, 8.9e-5},
	{ DeviceType::LIME_PCIE, 4, 4, 4.8e-5},

	{ DeviceType::Undef, 0, 0, 0.0}
};


DeviceType parse_device_type(const String& board) {
	if (board.equals("LimeSDR-USB")) return DeviceType::LIME_USB;
	return DeviceType::Undef;
}

double get_dev_offset(DeviceType type, int rx_sps, int tx_sps) {
	for (int i=0; uhd_offsets[i].type != DeviceType::Undef; ++i) {
		if (type == uhd_offsets[i].type && rx_sps == uhd_offsets[i].rx_sps && tx_sps == uhd_offsets[i].tx_sps)
			return uhd_offsets[i].offs;
	}
	LOGW("This configuration not supported (type=%d,rx_sps=%d,tx_sps=%d)", type, rx_sps, tx_sps);
	return 0.0;
}
}

int SampleBuffer::space() const {
	return capacity - len;
}
int SampleBuffer::available(jlong t) const {
	if (t < tm0) return -1; // past
	jlong tm1 = tm0 + len;
	if (t >= tm1) return 0; // future
	return (int)(tm1 - t);  // number of samples
}

// write samples (first sample in buf has time=t)
int SampleBuffer::write(short *b, int l, jlong t) {
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
	else if (tm1 < t) {
		LOGW("Making Gap in data");
		int l1 = (int)(t-tm1);
		int i1=(idx+l1)%capacity;
		if (i1+l1 <= capacity) memset(buf+i1, 0, l1*sizeof(short));
		else {
			int rem = capacity-i1;
			memset(buf + i1, 0, rem*sizeof(short));
			memset(buf, 0, (l1-rem)*sizeof(short));
		}
	}

	int i0=(idx+(int)(t-tm0))%capacity;
	if (i0+l <= capacity) {
		memcpy(buf+i0, b, l*sizeof(short));
	}
	else {
		int rem = capacity-i0;
		memcpy(buf + i0, b, rem*sizeof(short));
		memcpy(buf, b + rem, (l-rem)*sizeof(short));
	}
	if (tm1 < t + l) tm1 = t + l;
	len = (int)(tm1-tm0);
	if (len > capacity) {
		idx = (i0+len+capacity)%capacity;
		len = capacity;
		tm0 = t-capacity;
		LOGW("Buffer overflow");
	}
	return l;
}

// read samples starting from t
// after return first sample in buffer has timestamp = t
int SampleBuffer::read(short *b, int l, jlong t) {
	if (l <= 0) throw RuntimeException("wrong length "+l);
	if (t < tm0) return -1; // past data
	jlong tm1 = tm0 + len;
	if (t >= tm1) return 0; //future data

	int n = (int)(tm1-t); //number of available samples (from t to tm1)
	if (l > n) l = n;

	int i0=(idx+(int)(t-tm0))%capacity;
	if (i0+l <= capacity) {
		memcpy(b, buf+i0, l*sizeof(short));
	}
	else {
		int rem = capacity-i0;
		memcpy(b, buf + i0, rem*sizeof(short));
		memcpy(b + rem, buf, (l-rem)*sizeof(short));
	}
	idx = (i0 + l)%capacity;
	tm0 = t + l;
	len = (int)(tm1-tm0);
	return l;
}

RadioDevice::RadioDevice(int rx_sps, int tx_sps) {
	this->rx_sps = rx_sps = 4; //FIXME configuration
	this->tx_sps = tx_sps;
}
RadioDevice::~RadioDevice() {
	if (usrp_dev) close();
}
String RadioDevice::toString() const {
	return String::format("%s", usrp_dev->get_mboard_name().c_str());
}

boolean RadioDevice::open(const String& args) {
	rx_pkt_cnt = tx_pkt_cnt = 0;
	// Find UHD devices
	uhd::device_addr_t addr(args.intern());
	uhd::device_addrs_t dev_addrs = uhd::device::find(addr);
	if (dev_addrs.size() == 0) {
	    LOGE("No UHD devices found with address '%s'", args.cstr());
	    return false;
	}

	LOGD("Using discovered UHD device %s", dev_addrs[0].to_string().c_str());
	try {
	    usrp_dev = uhd::usrp::multi_usrp::make(addr);
	} catch(...) {
	    LOGE("UHD make failed, device '%s'", args);
	    return false;
	}

	uhd::property_tree::sptr prop_tree = usrp_dev->get_device()->get_tree();
	String board = usrp_dev->get_mboard_name();
	//String dev_name = prop_tree->access<std::string>("/name").get();
	devType = parse_device_type(board);
	if (devType == DeviceType::Undef) {
	    LOGE("Parse device type failed for '%s'", board.cstr());
		return false;
	}

	if (devType == DeviceType::B210) chans = 2;
	else if (devType == DeviceType::UMTRX) chans = 2;
	else chans = 1;
	LOGD("DeviceType %d, chans=%d", devType, chans);

	rx_gain = Array<double>(chans);
	tx_gain = Array<double>(chans);
	rx_freq = Array<double>(chans);
	tx_freq = Array<double>(chans);
	rx_buffer = Array<SampleBuffer>(chans);

	// set master clock
	double master_clock_freq = 8000000.0; // 8MHz = minimal/defaeult master clock
	if (devType == DeviceType::LIME_USB) {
		master_clock_freq = GSMRATE * 32;
		rx_rate = GSMRATE * rx_sps;
		tx_rate = GSMRATE * tx_sps;
	}
	else if (devType == DeviceType::LIME_PCIE) {
		master_clock_freq = GSMRATE * 32 * 3;
		rx_rate = GSMRATE * rx_sps * 3;
		tx_rate = GSMRATE * tx_sps * 3;
	}
	usrp_dev->set_master_clock_rate(master_clock_freq);
	double actual_clock = usrp_dev->get_master_clock_rate();
	master_clock_offset = actual_clock - master_clock_freq;
	if (Math::abs(master_clock_offset) > 1.0) {
		LOGE("Failed to set master clock rate %.2lf", MHz(master_clock_freq));
		return false;
	}
	LOGD("master_clock_freq %.2lf MHz(err=%.2lf)  rx/tx rate %.2lf/%.2lf MHz",
			MHz(actual_clock), MHz(master_clock_offset), MHz(rx_rate), MHz(tx_rate));

	// set rx/tx rate
	usrp_dev->set_rx_rate(rx_rate);
	usrp_dev->set_tx_rate(tx_rate);
	rx_rate = usrp_dev->get_rx_rate();
	tx_rate = usrp_dev->get_tx_rate();

	// set rx/tx bandwidth
	if (devType == DeviceType::LIME_USB || devType == DeviceType::LIME_PCIE) {
		for (int i = 0; i < chans; i++) {
			usrp_dev->set_tx_bandwidth(5e6, i);
			usrp_dev->set_rx_bandwidth(5e6, i);
		}
	}

	// get rx/tx streams
	uhd::stream_args_t stream_args;
	if (devType == DeviceType::LIME_USB || devType == DeviceType::LIME_PCIE) {
		stream_args = uhd::stream_args_t("sc12");
		stream_args.args["latency"] = (devType == DeviceType::LIME_USB) ? "0.0" : "0.3";
	}
	else {
		stream_args = uhd::stream_args_t("sc16");
	}
	//stream_args = uhd::stream_args_t("sc16"); //TODO check one setting for all
	for (int i = 0; i < chans; i++)
		stream_args.channels.push_back(i);

	rx_stream = usrp_dev->get_rx_stream(stream_args);
	tx_stream = usrp_dev->get_tx_stream(stream_args);

	// samples per packet
	//int rx_spp = (int)rx_stream->get_max_num_samps();
	//int tx_spp = (int)tx_stream->get_max_num_samps();
	//LOGD("rx_spp=%d, tx_spp=%d", rx_spp, tx_spp);

	//set timing offset
	double offs = get_dev_offset(devType, rx_sps, tx_sps);
	ts_offs = (jlong)(offs * rx_rate);

	int buf_len = SAMPLE_BUF_SZ / sizeof(uint32_t);
	for (int i = 0; i < rx_buffer.length; ++i) {
		rx_buffer[i] = std::move(SampleBuffer(buf_len, rx_rate));
	}

	//set rx/tx gains
	uhd::gain_range_t range = usrp_dev->get_rx_gain_range();
	for (int i = 0; i < rx_gain.length; ++i) {
		double gain = (range.start() + range.stop()) / 2;
		usrp_dev->set_rx_gain(gain, i);
		rx_gain[i] = usrp_dev->get_rx_gain(i);
	}
	range = usrp_dev->get_tx_gain_range();
	for (int i = 0; i < tx_gain.length; ++i) {
		double gain = (range.start() + range.stop()) / 2;
		usrp_dev->set_tx_gain(gain, i);
		tx_gain[i] = usrp_dev->get_tx_gain(i);
	}

	// print usrp configuration
	//LOGN("USRP config:\n%s", usrp_dev->get_pp_string().c_str());

	uhd::stream_cmd_t cmd = uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE;
	cmd.num_samps = rx_stream->get_max_num_samps()*2;
	cmd.stream_now = true;
	usrp_dev->issue_stream_cmd(cmd);

	//reset the tick counter offset to 0 to avoid getting
	usrp_dev->set_time_now(0.0);

	restart();
	return true;
}

void RadioDevice::close() {
	LOGD("RadioDevice::close");
	if (usrp_dev) {
		uhd::stream_cmd_t stream_cmd = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
		usrp_dev->issue_stream_cmd(stream_cmd);

		rx_stream.reset();
		tx_stream.reset();
		usrp_dev.reset();
	}
	else {
		LOGW("RadioDevice::stop - not running");
	}
}

boolean RadioDevice::setAntenna(const String& rx, const String& tx) {
	if (!usrp_dev) throw IllegalStateException("Device not opened");
	usrp_dev->set_rx_antenna("TX/RX");  // possible: ["TX/RX", "RX2", "CAL"]
	usrp_dev->set_tx_antenna("TX/RX");  // possible: ["TX/RX", "RX2", "CAL"]
	return true;
}
boolean RadioDevice::setFreq(double freq, int chan, bool tx) {
	if (!usrp_dev) throw IllegalStateException("Device not opened");
	LOGD("RadioDevice::setFreq(f=%.2lf,ch=%d,%s)", MHz(freq), chan, tx?"TX":"RX");
	uhd::tune_request_t treq = uhd::tune_request_t(freq, master_clock_offset);
	if (tx) {
		usrp_dev->set_tx_freq(treq, chan);
		tx_freq[chan] = freq;
	}
	else {
		usrp_dev->set_rx_freq(treq, chan);
		rx_freq[chan] = freq;
	}
	return true;
}

void RadioDevice::restart() {
	if (!usrp_dev) throw IllegalStateException("Device not opened");
	double delay = 0.1;
	uhd::time_spec_t current = usrp_dev->get_time_now();
	uhd::stream_cmd_t cmd = uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS;
	cmd.stream_now = false;
	cmd.time_spec = uhd::time_spec_t(current.get_real_secs() + delay);
	usrp_dev->issue_stream_cmd(cmd);
	rx_flush(10);
}

void RadioDevice::rx_flush(int num_pkts) {
	if (!usrp_dev) throw IllegalStateException("Device not opened");
	uhd::rx_metadata_t md;
	double timeout = 0.5; //500ms
	int rx_spp = (int)rx_stream->get_max_num_samps(); // samples per packet
	short dummy[2*rx_spp];

	std::vector<short *> pkt_ptrs;
	for (int i = 0; i < chans; i++)
		pkt_ptrs.push_back(dummy);

	if (num_pkts <= 0) num_pkts=1;
	while (num_pkts-- > 0) {
		rx_stream->recv(pkt_ptrs, rx_spp, md, timeout, true);
		if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
			LOGE("recv error = %s(%d)", md.strerror().c_str(), md.error_code);
		}
		if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
			LOGE("recv timeout");
			break;
		}
	}
	LOGD("rx_flush done");
}

void RadioDevice::recv() {
	if (!usrp_dev) throw IllegalStateException("Device not opened");
	uhd::rx_metadata_t md;
	int rx_spp = (int)rx_stream->get_max_num_samps(); // samples per packet
	short pkt_bufs[chans][2*rx_spp];

	std::vector<short *> pkt_ptrs;
	for (int i = 0; i < chans; i++)
		pkt_ptrs.push_back(pkt_bufs[i]);

	LOGD("want samples = %d", rx_buffer[0].space());
	//feed rx_buffer
	while (rx_buffer[0].space() > rx_spp) {
		int num_smpls = (int)rx_stream->recv(pkt_ptrs, rx_spp, md, 0.1, true);
		if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
			LOGE("recv error = %s(%d)", md.strerror().c_str(), md.error_code);
			if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) break;
			continue;
		}
		LOGD("recv pkt %d samples = %d", rx_pkt_cnt, num_smpls);
		++rx_pkt_cnt;

		jlong ts = md.time_spec.to_ticks(rx_rate);
		for (int i = 0; i < rx_buffer.length; ++i) {
			rx_buffer[i].write(pkt_bufs[i], num_smpls, ts);
		}
	}
}

void RadioDevice::send() {
	
}
