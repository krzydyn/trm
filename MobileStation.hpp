#ifndef MOBILESTATION_HPP
#define MOBILESTATION_HPP

#include <lang/Exception.hpp>
#include <nio/channels/Channel.hpp>

using namespace nio::channels;
class MobileStation : extends Object {
private:
	static const int DATA_RECV_SIZE = 158;
	static const int DATA_SEND_SIZE = 154;
	static const int FRAME_MODULUS = 2715648;
	static const int CLOCK_ADVANCE = 20;

	static const String trxHost;
	static const int trxPort = 5700;

	enum class Command {
		POWEROFF,
		RXTUNE,
		TXTUNE,
		SETTSC,
		SETBSIC,
		POWERON,
		SETRXGAIN,
		SETPOWER,
		SETSLOT,
		
	};

	Shared<Selector> selector;
	Shared<DatagramChannel> clockChn;
	Shared<DatagramChannel> ctrlChn;
	Shared<DatagramChannel> dataChn;

	void sendCommand(const Command cmd, int param = 0);
	void sendData(uint8_t tn, uint32_t fn, uint8_t gain, nio::ByteBuffer& data);

	void handleResponse(const String& rsp);
	void handleClock(int clk);
	void handleData(nio::ByteBuffer& data);

	void run();
public:
	MobileStation();
	void start();
};



#endif
