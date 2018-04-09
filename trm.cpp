#include "MobileStation.hpp"
#include "RadioDevice.hpp"


#define GSMRATE (1625000.0 / 6.0)
#define SAMPLE_BUF_SZ    (1<<20)

jlong readTimestamp = 0;
jlong writeTimestamp = 0;

int writeBuffer(SampleBuffer& b) {
	int segmentLen = 100; // =sendBuffer[0]->getSegmentLen();
	short segment[segmentLen];

	int r = b.write(segment, segmentLen, writeTimestamp);
	LOGD("Wrote segment len=%d = %d, b = %s", segmentLen, r, b.toString().cstr());
	if (r != segmentLen) {
		LOGE("can't write segment r=%d", r);
		return 0;
	}
	writeTimestamp += r;
	return r;
}
int readBuffer(SampleBuffer& b) {
	int segmentLen = 100;// =recvBuffer[0]->getSegmentLen();
	short segment[segmentLen];

	LOGD("Read segment len=%d b = %s", segmentLen, b.toString().cstr());
	int r = b.read(segment, segmentLen, readTimestamp);
	if (r != segmentLen) {
		LOGE("can't read segment r=%d", r);
		return 0;
	}
	readTimestamp += r;
	return r;
}
void runTests() {
	double rate = GSMRATE;
	int buf_len = SAMPLE_BUF_SZ / sizeof(uint32_t);
	int rx_sps = 4; //Samples-per-symbol

	double rx_rate = rate * rx_sps;

	SampleBuffer b(buf_len, rx_rate);

	readTimestamp = writeTimestamp = 121;
	for (int i=0; i < 10; ++i) {
		--writeTimestamp;
		writeBuffer(b);
	}
	for (int i=0; i < 10; ++i) {
		--readTimestamp;
		readBuffer(b);
	}
}

int main(int argc, const char *argv[]) {
	if (argc > 1 && strcmp(argv[1],"-t")==0) {
		runTests();
		return 0;
	}
	MobileStation ms;
	ms.start();	
	return 0;
}
