#include "pch.h"

#include "compressor.h"

void test_huffman() {
    {
        const char *type = "huff";
        {

            string s0 = "abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd";
            string s1 = ICompressor::compressString(s0, type);
            assert(s1.size() < s0.size());
            string s2 = ICompressor::uncompressString(s1, type);
            assert(s0 == s2);
        }
    }
}

