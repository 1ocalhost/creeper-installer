#include <shlwapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <string>
#include <fstream>
#include "unzip.hpp"
#include "linker.hpp"

#define APP_NAME L"Creeper"
#define APP_UID L"creeper.pyapp.win32"

typedef void (*ProcessEnumHandler)(const PROCESSENTRY32& entry);

class Path : public std::wstring {
public:
	Path(PCWSTR path) : std::wstring(path) {}

	Path operator /(PCWSTR part) {
		Path newPath(*this);
		if (newPath.length() >= 1 && (*newPath.rbegin() != L'\\'))
			newPath.push_back(L'\\');
		newPath.append(part);
		return newPath;
	}

	void mkdir() {
		CreateDirectory(c_str(), NULL);
	}

	operator PCWSTR() const {
		return c_str();
	}
};

class SelfAttachedFiles {
	typedef std::fstream S;

	// offset[4] + length[4] + checksum[4]
	static const size_t META_DATA_NUM = 3;

public:
	bool Init() {
		WCHAR path[MAX_PATH] = { 0 };
		HMODULE hModule = GetModuleHandle(NULL);
		if (hModule)
			GetModuleFileName(hModule, path, ARRAYSIZE(path));

		self_file_.open(path, S::in | S::binary);
		if (!self_file_)
			ErrorMsg(L"Failed to open: %s", path);

		return (bool)self_file_;
	}

	bool ExtractBackTo(const Path& path) {
		DWORD32 offset = 0, length = 0;
		if (!ReadBackItemInfo(&offset, &length)) {
			ErrorMsg(L"Invalid checksum for: %s", (PCWSTR)path);
			return false;
		}
		ExtractFile(offset, length, path);
		return true;
	}

	bool PushBackTo(PCWSTR newAttach, PCWSTR output) {
		std::ifstream attach(newAttach, S::in | S::binary);
		if (!attach) {
			ErrorMsg(L"Failed to open: %s", newAttach);
			return false;
		}

		std::ofstream out(output, S::out | S::binary);
		if (!out) {
			ErrorMsg(L"Failed to create: %s", output);
			return false;
		}

		out << self_file_.rdbuf();
		out << attach.rdbuf();
		out << GetMetaData(&attach);
		return true;
	}

private:
	std::string GetMetaData(std::ifstream* attach) {
		self_file_.seekg(0, S::end);
		attach->seekg(0, S::end);
		DWORD32 self_size = (DWORD32)self_file_.tellg();
		DWORD32 attach_size = (DWORD32)attach->tellg();

		DWORD32 data[META_DATA_NUM] = { 0 };
		data[0] = (DWORD32)htonl(self_size);
		data[1] = (DWORD32)htonl(attach_size);
		data[2] = (DWORD32)htonl(self_size ^ attach_size);
		return std::string((char*)data, sizeof(data));
	}

	bool ReadBackItemInfo(DWORD32* offset, DWORD32* length) {
		DWORD32 data[META_DATA_NUM] = { 0 };
		size_t data_offset = extracted_len + sizeof(data);
		self_file_.seekg((std::streamoff)-1 * data_offset, S::end);
		self_file_.read((char*)data, sizeof(data));

		*offset = ntohl(data[0]);
		*length = ntohl(data[1]);
		DWORD32 checksum = ntohl(data[2]);
		extracted_len += (*length + sizeof(data));
		return (*offset ^ *length) == checksum;
	}

	void ExtractFile(DWORD32 offset, DWORD32 length, const Path& path) {
		std::ofstream out(path, S::out | S::binary);
		self_file_.seekg(offset);

		const int BUF_SIZE = 1024 * 4;
		char buf[BUF_SIZE];
		size_t rest_len = length;
		while (rest_len) {
			size_t block_size = min(rest_len, BUF_SIZE);
			rest_len -= block_size;
			self_file_.read(buf, block_size);
			if (!self_file_)
				break;
			out.write(buf, block_size);
		}
	}

	std::ifstream self_file_;
	size_t extracted_len = 0;
};

BOOL RemoveDir(PCWSTR path) {
	if (!PathFileExists(path))
		return TRUE;

	SHFILEOPSTRUCT shfo = {
		NULL,
		FO_DELETE,
		path,
		NULL,
		FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION,
		FALSE,
		NULL,
		NULL
	};

	return SHFileOperation(&shfo) == 0;
}

void EnumProcess(ProcessEnumHandler handler) {
	HANDLE hProcessSnap;
	PROCESSENTRY32 pe32;
	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
		return;

	pe32.dwSize = sizeof(PROCESSENTRY32);
	if (Process32First(hProcessSnap, &pe32)) {
		handler(pe32);
		while (Process32Next(hProcessSnap, &pe32)) {
			handler(pe32);
		}
		CloseHandle(hProcessSnap);
	}
}

void KillOldProcesses(const PROCESSENTRY32& entry) {
	if (!entry.th32ProcessID)
		return;

	HANDLE hProcess = OpenProcess(
		PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE,
		FALSE, entry.th32ProcessID);

	if (!hProcess)
		return;

	WCHAR filePath[MAX_PATH] = { 0 };
	GetModuleFileNameEx(hProcess, NULL, filePath, MAX_PATH);

	if (wcsstr(filePath, APP_UID)) {
		TerminateProcess(hProcess, 0);
	}

	CloseHandle(hProcess);
}

BOOL CreateNewFolder(PCWSTR path) {
	if (!RemoveDir(path)) {
		ErrorMsg(L"Failed to remove: %s", path);
		return FALSE;
	}

	CreateDirectory(path, NULL);
	return TRUE;
}

void GetTempDirPath(PWSTR path, DWORD size) {
	DWORD length = GetTempPath(size, path);
	wsprintf(path + length, APP_UID);
}

void GetAppDirPath(PWSTR path, DWORD size) {
	if (size < MAX_PATH) {
		ErrorMsg(L"GetTempDirPath: size < MAX_PATH");
		return;
	}

	SHGetSpecialFolderPath(
		NULL, path, CSIDL_LOCAL_APPDATA, TRUE);
	wsprintf(path + wcslen(path), L"\\%s", APP_UID);
}

void SelfExtractAndExec(Path tempPath, Path appPath) {
	Path python_zip = tempPath / L"python.zip";
	Path app_zip = tempPath / L"app.zip";

	SelfAttachedFiles saf;
	if (!saf.Init())
		return;

	if (!saf.ExtractBackTo(app_zip))
		return;

	if (!saf.ExtractBackTo(python_zip))
		return;

	Path python_dir = appPath / L"python";
	python_dir.mkdir();

	if (!Unzip(python_zip, python_dir))
		return;

	if (!Unzip(app_zip, appPath))
		return;

	ShellExecute(NULL, NULL,
		python_dir / L"pythonw.exe",
		appPath / L"install.py", NULL, 0);
}

BOOL Unpack() {
	WCHAR tempPath[MAX_PATH] = { 0 };
	GetTempDirPath(tempPath, ARRAYSIZE(tempPath));
	if (!CreateNewFolder(tempPath))
		return FALSE;

	WCHAR appPath[MAX_PATH] = { 0 };
	GetAppDirPath(appPath, ARRAYSIZE(appPath));
	EnumProcess(&KillOldProcesses);
	if (!CreateNewFolder(appPath))
		return FALSE;

	SelfExtractAndExec(tempPath, appPath);
	return TRUE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nShowCmd) {
	int result = 0;
	int nArgs = 0;
	LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
	if (szArglist && nArgs == 4
			&& (std::wstring(L"push") == szArglist[1])) {
		SelfAttachedFiles saf;
		if (!saf.Init())
			return 4;

		PCWSTR newAttach = szArglist[2];
		PCWSTR output = szArglist[3];
		result = saf.PushBackTo(newAttach, output) ? 0 : 3;
	}
	else {
		PCWSTR question = L"Do you want to install " APP_NAME L" now?";
		if (IDYES != MessageBox(NULL, question, APP_NAME, MB_YESNO))
			result = 2;
		else
			result = Unpack() ? 0 : 1;
	}

	LocalFree(szArglist);
	return result;
}
