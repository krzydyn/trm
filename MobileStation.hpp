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

	const String trxHost;
	const int trxPort;

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

	boolean running = false;
	boolean transceiverAvailable = false;
	boolean setupDone = false;
	uint32_t trxFrame;
	uint32_t currentFrame;

	uint32_t nextFrame();

	void sendCommand(const Command cmd, int param = 0);
	void sendData(uint8_t tn, uint32_t fn, uint8_t gain, nio::ByteBuffer& data);

	void handleResponse(const String& rsp);
	void handleClock(int clk);
	void handleData(nio::ByteBuffer& data);

	void sendDummyPacket();
	void setupTrx();
	void run();
public:
	static const int DAFAULT_TRX_PORT = 5700;

	MobileStation(String host, int port=DAFAULT_TRX_PORT) : trxHost(host), trxPort(port) {
	}
	void start();
};



#endif
