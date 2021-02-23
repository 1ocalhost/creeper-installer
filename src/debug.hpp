#pragma once

#ifdef _DEBUG
#define IS_DEBUG 1

#include <iostream>
#include <string>
#include <locale>
#include <codecvt>

std::wstring u8to16(const std::string& u8) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> cvt;
	return cvt.from_bytes(u8);
}

#else
#define IS_DEBUG 0
#endif