// #include "xysj_StableHeaders.h"
#include "stdafx.h"

#include "ScanUtil.h"

namespace Scan
{
    int split(StringVec &result, const String &src, const String& delm)
    {
        String _src(src);
        char *p = strtok((char*)_src.c_str(), delm.c_str());
        while (p != NULL)
        {
            result.push_back(p);
            p = strtok(NULL, delm.c_str());
        }
        return int(result.size());
    }

    const String trim(const String& src)
    {
        if (src.empty())
        {
            return "";
        }

        size_t start = 0;
        size_t end = src.size() - 1;
        while (start <= end && src[start] == ' ')
        {
            ++start;
        }
        while (start <= end && src[end] == ' ')
        {
            --end;
        }
        return src.substr(start, end + 1 - start);
    }

    const String replace(const String& src, const String& oldSub, const String& newSub)
    {
        String ret;
        size_t begin = 0, end = 0;
        while ((end = src.find(oldSub, begin)) != -1)
        {
            ret += src.substr(begin, end - begin) + newSub;
            begin = end + oldSub.size();
        }
        ret += src.substr(begin, src.size() - begin);
        return ret;
    }
}