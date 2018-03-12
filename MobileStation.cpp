#include <lang/Number.hpp>
#include "MobileStation.hpp"

namespace {
int makeParam(int a, int b) { return ((a&0xff)<<8) | (b&0xff); }
void setupChannel(Shared<DatagramChannel> chn, const String& host, int port) {
	chn->bind(InetSocketAddress(host, port+100));
	chn->connect(InetSocketAddress(host, port));
}

byte dummy_burst[148] = {
    0,0,0,
    1,1,1,1,1,0,1,1,0,1,1,1,0,1,1,0,0,0,0,0,1,0,1,0,0,1,0,0,1,1,1,0,
    0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,1,1,1,0,0,
    0,1,0,1,1,1,0,0,0,1,0,1,1,1,0,0,0,1,0,1,0,1,1,1,0,1,0,0,1,0,1,0,
    0,0,1,1,0,0,1,1,0,0,1,1,1,0,0,1,1,1,1,0,1,0,0,1,1,1,1,1,0,0,0,1,
    0,0,1,0,1,1,1,1,1,0,1,0,1,0,
    0,0,0,
};
}

uint32_t MobileStation::nextFrame() {
	currentFrame += CLOCK_ADVANCE;
	return currentFrame;
}

void MobileStation::sendCommand(Command cmd, int param) {
	if (!transceiverAvailable && cmd != Command::POWEROFF) {
		LOGE("transceiver not available");
		return ;
	}
	String msg = "CMD ";
	switch (cmd) {
	case Command::POWEROFF:
		msg += "POWEROFF";
		break;
	case Command::RXTUNE:
		msg += String::format("RXTUNE %d", param);
		break;
	case Command::TXTUNE:
		msg += String::format("TXTUNE %d", param);
		break;
	case Command::SETTSC:
		msg += String::format("SETTSC %d", param);
		break;
	case Command::SETBSIC:
		msg += String::format("SETBSIC %d", param);
		break;
	case Command::POWERON:
		msg += "POWERON";
		break;
	case Command::SETRXGAIN:
		msg += String::format("SETRXGAIN %d", param);
		break;
	case Command::SETPOWER:
		msg += String::format("SETPOWER %d", param);
		break;
	case Command::SETSLOT:
		msg += String::format("SETSLOT %d %d", (param>>8)&0xff, param&0xff);
		break;
	}
	msg += '\0';
	Array<byte> bytes = msg.getBytes();
	ctrlChn->write(*nio::ByteBuffer::wrap(bytes));
}

void MobileStation::run() {
	int maxfd = clockChn->getFDVal();
	if (maxfd < ctrlChn->getFDVal()) maxfd = ctrlChn->getFDVal();
	if (maxfd < dataChn->getFDVal()) maxfd = dataChn->getFDVal();

	transceiverAvailable = true;
	Shared<nio::ByteBuffer> buf = nio::ByteBuffer::allocate(1000);
	fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(clockChn->getFDVal(), &readfds);
		FD_SET(ctrlChn->getFDVal(), &readfds);
		FD_SET(dataChn->getFDVal(), &readfds);
	running = true;
	while (running) {
		struct timeval tv = {1, 0};

		int n = select(maxfd + 1, &readfds, NULL, NULL, &tv);
		if (n == 0) {
			if (transceiverAvailable) LOGE("Nothing received (transceiver not available)");
			transceiverAvailable = false;
			setupDone = false;
			sendCommand(Command::POWEROFF);
			continue;
		}
		if (n == -1) {
			throw io::IOException(String("select") + strerror(errno));
		}
		if (FD_ISSET(clockChn->getFDVal(), &readfds)) {
			buf->clear();
			clockChn->receive(*buf);
			Array<byte>& a = buf->array();
			int clock = 0;
			if (sscanf((const char *)&a[0], "IND CLOCK %u", &clock) == 1) {
				handleClock(clock);
			}
			else {
				LOGE("Unknown clock message");
			}
		}
		if (FD_ISSET(ctrlChn->getFDVal(), &readfds)) {
			buf->clear();
			ctrlChn->receive(*buf);
			Array<byte>& a = buf->array();
			handleResponse(String(a, 0, a.length));
		}
		if (FD_ISSET(dataChn->getFDVal(), &readfds)) {
			buf->clear();
			dataChn->receive(*buf);
			buf->flip();
			handleData(*buf);
		}
	}
}

// osmo-bts-trx/trx_if.c(462) trx_if_data
void MobileStation::sendData(uint8_t tn, uint32_t fn, uint8_t gain, nio::ByteBuffer& data) {
	if (!transceiverAvailable) {
		LOGE("transceiver not available");
		return ;
	}
	if (tn < 0 || tn > 7) throw IllegalArgumentException(String::format("TN=%d", tn));

	Shared<nio::ByteBuffer> buf = nio::ByteBuffer::allocate(1000);
	buf->put(tn);    // timeslot number (0..7)
	buf->putInt(fn); // frame number
	buf->put(gain);  // signal power
	while (data.position() < data.limit()) {
		byte x = data.get();
		buf->put(x);
	}
	while (buf->position() < DATA_SEND_SIZE) {
		buf->put(0);
	}
	buf->flip();
	LOGD("SendData: %d bytes", buf->limit());
	dataChn->write(*buf);
}
void MobileStation::handleResponse(const String& resp) {
	LOGD("Response: %s", resp.cstr());
}
void MobileStation::handleClock(int clk) {
	if (!transceiverAvailable) {
		trxFrame = clk;
	}
	transceiverAvailable = true;
	LOGD("Clock: %d", clk);
	if (!setupDone) setupTrx();
	else sendDummyPacket();
}
void MobileStation::handleData(nio::ByteBuffer& data) {
	int pos = data.position();
	int lim = data.limit();
	int rem = (pos <= lim ? lim - pos : 0);
	if (rem != DATA_RECV_SIZE) {
		LOGE("Wrong data length: %d", rem);
		return ;
	}
	uint8_t tn = data.get();      // timeslot number
	uint32_t fn = data.getInt();  // frame number
	int8_t rssi = (uint8_t)-data.get();
	float toa = data.getShort()/256.0f;
	if (tn < 0 || tn > 7) {
		LOGE("Invalid TN = %d", tn);
		return ;
	}

	LOGD("[%d bytes] tn=%d fn=%u rssi=%d  toa=%4.2f", rem, tn, fn, rssi, toa);
	StringBuilder sb(2*DATA_RECV_SIZE);
	while (data.position() < data.limit()) {
		int x = 127 - (data.get()&0xff);
		sb.append(Integer::toString(x));
		sb.append(",");
	}
	LOGD("Data: %s", sb.toString().cstr());
}

void MobileStation::sendDummyPacket() {
	Shared<nio::ByteBuffer> data = nio::ByteBuffer::allocate(148);
	for (int i = 0; i < 148; ++i) data->put(dummy_burst[i]);
	byte tn = 1;
	uint32_t fn = nextFrame();
	sendData(tn, fn, 0, *data);
}

void MobileStation::setupTrx() {
	sendCommand(Command::POWEROFF);
	//sendCommand(Command::RXTUNE, 885400); //set by bts
	//sendCommand(Command::TXTUNE, 930400);
	sendCommand(Command::TXTUNE, 885400); // for arfcn=1001
	sendCommand(Command::RXTUNE, 930400);

	//GSM1800
	//sendCommand(Command::TXTUNE, 1710200);
	//sendCommand(Command::RXTUNE, 1805200);

	sendCommand(Command::SETTSC, 7);
	sendCommand(Command::SETBSIC, 63);
	sendCommand(Command::POWERON);
	sendCommand(Command::SETRXGAIN, 10);
	sendCommand(Command::SETPOWER, 0);
	sendCommand(Command::SETSLOT, makeParam(0, 5));
	sendCommand(Command::SETSLOT, makeParam(1, 7));
	sendCommand(Command::SETSLOT, makeParam(2, 1));
	sendCommand(Command::SETSLOT, makeParam(3, 1));
	sendCommand(Command::SETSLOT, makeParam(4, 1));
	sendCommand(Command::SETSLOT, makeParam(5, 1));
	sendCommand(Command::SETSLOT, makeParam(6, 1));
	sendCommand(Command::SETSLOT, makeParam(7, 1));
	setupDone = true;
}

void MobileStation::start() {
	selector = Selector::open();
	clockChn = selector->provider()->openDatagramChannel();
	ctrlChn = selector->provider()->openDatagramChannel();
	dataChn = selector->provider()->openDatagramChannel();
	setupChannel(clockChn, trxHost, trxPort);
	setupChannel(ctrlChn, trxHost, trxPort+1);
	setupChannel(dataChn, trxHost, trxPort+2);
	run();
}