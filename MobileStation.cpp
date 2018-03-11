#include "MobileStation.hpp"

namespace {
int running = false;
int makeParam(int a, int b) { return ((a&0xff)<<8) | (b&0xff); }
void setupChannel(Shared<DatagramChannel> chn, const String& host, int port) {
	chn->bind(InetSocketAddress(host, port+100));
	chn->connect(InetSocketAddress(host, port));
}
}

const String MobileStation::trxHost = "localhost";

MobileStation::MobileStation() {
	selector = Selector::open();
	clockChn = selector->provider()->openDatagramChannel();
	ctrlChn = selector->provider()->openDatagramChannel();
	dataChn = selector->provider()->openDatagramChannel();
}

void MobileStation::sendCommand(Command cmd, int param) {
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

void MobileStation::sendData(nio::ByteBuffer& data) {
// osmo-bts-trx/trx_if.c(462) trx_if_data
}

void MobileStation::run() {
	sendCommand(Command::POWEROFF);
	sendCommand(Command::RXTUNE, 885400);
	sendCommand(Command::TXTUNE, 930400);
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

	int maxfd = clockChn->getFDVal();
	if (maxfd < ctrlChn->getFDVal()) maxfd = ctrlChn->getFDVal();
	if (maxfd < dataChn->getFDVal()) maxfd = dataChn->getFDVal();

	Shared<nio::ByteBuffer> buf = nio::ByteBuffer::allocate(2000);
	fd_set readfds;
	running = true;
	while (running) {
		struct timeval tv = {1, 0};
		FD_ZERO(&readfds);
		FD_SET(clockChn->getFDVal(), &readfds);
		FD_SET(ctrlChn->getFDVal(), &readfds);
		FD_SET(dataChn->getFDVal(), &readfds);
		
		int n = select(maxfd + 1, &readfds, NULL, NULL, &tv);
		if (n == 0) {
			Log.log("select idle");
			continue;
		}
		if (n == -1) {
			throw io::IOException(String("select") + strerror(errno));
		}
		if (FD_ISSET(clockChn->getFDVal(), &readfds)) {
			Log.log("clock ready");
			buf->clear();
			clockChn->receive(*buf);
			Array<byte>& a = buf->array();
			int clock = 0;
			if (sscanf((const char *)&a[0], "IND CLOCK %u", &clock) == 1) {
				handleClock(clock);
			}
			else {
				Log.log("Unknown clock message");
			}
		}
		if (FD_ISSET(ctrlChn->getFDVal(), &readfds)) {
			Log.log("control ready");
			buf->clear();
			ctrlChn->receive(*buf);
			Array<byte>& a = buf->array();
			handleResponse(String(a, 0, a.length));
		}
		if (FD_ISSET(dataChn->getFDVal(), &readfds)) {
			Log.log("data ready");
			buf->clear();
			dataChn->receive(*buf);
			buf->flip();
			handleData(*buf);
		}
	}
}

void MobileStation::handleResponse(const String& resp) {
	Log.log("Response: %s", resp.cstr());
}
void MobileStation::handleClock(int clk) {
	Log.log("Clock: %d", clk);
}
void MobileStation::handleData(nio::ByteBuffer& data) {
	int pos = data.position();
	int lim = data.limit();
	int rem = (pos <= lim ? lim - pos : 0);
	Log.log("Data: len=%d", rem);
	if (rem != DATA_RECV_SIZE) {
		throw RuntimeException("Wrong data length");
	}
}

void MobileStation::start() {
	setupChannel(clockChn, trxHost, trxPort);
	setupChannel(ctrlChn, trxHost, trxPort+1);
	setupChannel(dataChn, trxHost, trxPort+2);
	run();
}
