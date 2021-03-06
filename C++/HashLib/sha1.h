#ifndef SHA1_H
#define SHA1_H

#include <stdint.h>

#include <string>

class Sha1 {
public:
    static const int OUTPUT_BYTES = 20;
    static const int BLOCK_BYTES = 64;
    static const int LENGTH_BYTES = 8;

public:
    Sha1();
    void update(const void *buf, int n);
    void finalize();
    void digest(uint8_t out[OUTPUT_BYTES]); //big endian
    string digestStr();

private:
    uint32_t mH[OUTPUT_BYTES / sizeof(uint32_t)];
    uint8_t mBuf[BLOCK_BYTES];
    uint64_t mSize;
};

#endif
