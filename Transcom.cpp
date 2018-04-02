#include <lang/Number.hpp>
#include "Transcom.hpp"

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

uint32_t Transcom::nextFrame() {
	//currentFrame = (currentFrame + CLOCK_ADVANCE)%FRAME_MODULUS;
	currentFrame = (currentFrame + 1)%FRAME_MODULUS;
	return currentFrame;
}

void Transcom::sendCommand(Command cmd, int param) {
	if (!transceiverAvailable && cmd != Command::POWEROFF) {
		LOGE("transceiver not available, command '%d' not send", cmd);
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

void Transcom::run() {
	int maxfd = clockChn->getFDVal();
	if (maxfd < ctrlChn->getFDVal()) maxfd = ctrlChn->getFDVal();
	if (maxfd < dataChn->getFDVal()) maxfd = dataChn->getFDVal();

	transceiverAvailable = true;
	Shared<nio::ByteBuffer> buf = nio::ByteBuffer::allocate(1000);
	fd_set readfds;
	running = true;
	while (running) {
		//TODO use Selector
		struct timeval tv = {1, 0};
		FD_ZERO(&readfds);
		FD_SET(clockChn->getFDVal(), &readfds);
		FD_SET(ctrlChn->getFDVal(), &readfds);
		FD_SET(dataChn->getFDVal(), &readfds);

		int n = select(maxfd + 1, &readfds, NULL, NULL, &tv);
		if (n == 0) {
			if (transceiverAvailable) LOGW("Nothing received (transceiver not available)");
			transceiverAvailable = false;
			setupDone = false;
			sendCommand(Command::POWEROFF);
			continue;
		}
		if (n == -1) {
			throw io::IOException(String("select ") + strerror(errno));
		}
		if (FD_ISSET(clockChn->getFDVal(), &readfds)) {
			buf->clear();
			clockChn->receive(*buf);
			String msg(buf->array());
			try {
				if (!msg.startsWith("IND CLOCK ")) throw Exception();
				int clock = Integer::parseInt(msg.substring(10));
				handleClock(clock);
			}
			catch (const Exception& ex) {
				LOGE("Unrecognized clock message "+msg+"\n"+ex.toString());
			}
		}
		if (FD_ISSET(ctrlChn->getFDVal(), &readfds)) {
			buf->clear();
			ctrlChn->receive(*buf);
			handleResponse(String(buf->array()));
		}
		if (FD_ISSET(dataChn->getFDVal(), &readfds)) {
			buf->clear();
			dataChn->receive(*buf);
			buf->flip();
			handleData(*buf);
		}
	}
}

// based on osmo-bts-trx/trx_if.c(462) trx_if_data
void Transcom::sendData(uint8_t tn, uint32_t fn, uint8_t gain, nio::ByteBuffer& data) {
	if (!transceiverAvailable) {
		LOGE("transceiver not available, data not sent");
		return ;
	}
	if (tn < 0 || tn > 7) throw IllegalArgumentException(String::format("TN=%d", tn));

	Shared<nio::ByteBuffer> buf = nio::ByteBuffer::allocate(1000);
	buf->put(tn);    // timeslot number (0..7)
	buf->putInt(fn); // frame number
	buf->put(gain);  // signal power
	StringBuilder sb(2*DATA_RECV_SIZE);
	while (data.position() < data.limit()) {
		byte x = data.get();
		buf->put(x);
		sb.append(String::format("%u,",x));
	}
	while (buf->position() < DATA_SEND_SIZE) {
		buf->put(0);
		sb.append("N");
	}
	buf->flip();
	LOGD("sendData(tn=%d, fn=%d, gain=%d, bytes=%d", tn, fn, gain, buf->limit());
	LOGD(sb.toString());
	dataChn->write(*buf);
}
void Transcom::sendDummyPacket() {
	Shared<nio::ByteBuffer> data = nio::ByteBuffer::allocate(148);
	for (int i = 0; i < 148; ++i) data->put(dummy_burst[i]);
	byte tn = 0;
	data->flip();
	sendData(tn, nextFrame(), 0, *data);
	++tn; data->flip();
	sendData(tn, nextFrame(), 0, *data);
/*
	sendData(tn+2, nextFrame(), 0, *data);
	sendData(tn+3, nextFrame(), 0, *data);
	sendData(tn+4, nextFrame(), 0, *data);
*/
}

void Transcom::handleResponse(const String& resp) {
	LOGD("Response: %s", resp.cstr());
}
void Transcom::handleClock(int clk) {
	if (!transceiverAvailable) {
	}
	transceiverAvailable = true;
	LOGD("Clock: %d", clk);
	trxFrame = clk;
	currentFrame = trxFrame;
	currentFrame = (currentFrame + CLOCK_ADVANCE)%FRAME_MODULUS;
	if (!setupDone) setupTrx();
	else {
		jlong tm = System.currentTimeMillis();
		if (sendTm < tm) {
			sendTm = tm + 1000;
			sendDummyPacket();
		}
	}
}
void Transcom::handleData(nio::ByteBuffer& data) {
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
		sb.append(String::format("%d,",x));
	}
	LOGD("Data: %s", sb.toString().cstr());
}

void Transcom::setupTrx() {
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

void Transcom::start() {
	selector = Selector::open();
	clockChn = selector->provider()->openDatagramChannel();
	ctrlChn = selector->provider()->openDatagramChannel();
	dataChn = selector->provider()->openDatagramChannel();
	setupChannel(clockChn, trxHost, trxPort);
	setupChannel(ctrlChn, trxHost, trxPort+1);
	setupChannel(dataChn, trxHost, trxPort+2);
	run();
}
