#pragma once

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

#define snprintf sprintf_s
#define strcasecmp lstrcmp

#ifndef HINST
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST ((HINSTANCE)&__ImageBase)
#endif

template <class T>
T mabs(T no)
{
  return (no<0)?(no*-1):no;
}

template<class Interface>
inline void SafeRelease(Interface **ppInterfaceToRelease)
{
    if (*ppInterfaceToRelease != NULL) {
        (*ppInterfaceToRelease)->Release();
        (*ppInterfaceToRelease) = NULL;
    }
}
