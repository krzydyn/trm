#ifndef MOBILESTATION_HPP
#define MOBILESTATION_HPP

#include <nio/channels/Channel.hpp>

using namespace nio::channels;
class MobileStation : extends Object {
private:
	static const int DATA_RECV_SIZE = 158;
	static const int DATA_SEND_SIZE = 154;
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
	void sendData(nio::ByteBuffer& data);

	void handleResponse(const String& rsp);
	void handleClock(int clk);
	void handleData(nio::ByteBuffer& data);

	void run();
public:
	MobileStation();
	void start();
};



#endif
