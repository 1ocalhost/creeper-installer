#include <shlwapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <string>
#include <fstream>
#include "unzip.hpp"
#include "wait.hpp"
#include "linker.hpp"
#include "debug.hpp"

#define APP_NAME L"Creeper"
#define APP_VER g_appVersion
#define APP_UID L"creeper.pyapp.win32"

WCHAR g_appVersion[MAX_PATH] = L"{{app_version}}";

typedef void (*ProcessEnumHandler)(const PROCESSENTRY32& entry);

class Path : public std::wstring {
public:
	Path(PCWSTR path) : std::wstring(path) {}

	Path operator /(PCWSTR part) {
		size_t partLen = wcslen(part);
		if (partLen == 0
				|| (partLen == 1 && part[0] == L'.'))
			return *this;

		Path newPath(*this);
		if (!newPath.IsDir())
			newPath.push_back(L'\\');

		newPath.append(part);
		return newPath;
	}

	Path operator /(const std::wstring& part) {
		return *this / part.c_str();
	}

	BOOL MakeDir() {
		int result = SHCreateDirectoryEx(NULL, c_str(), NULL);
		return (result == ERROR_SUCCESS
			|| result == ERROR_FILE_EXISTS
			|| result == ERROR_ALREADY_EXISTS);
	}

	operator PCWSTR() const {
		return c_str();
	}

	bool IsDir() const {
		if (empty())
			return false;
		return (*rbegin() == L'\\' || *rbegin() == L'/');
	}

	std::wstring Name() const {
		PCWSTR path = c_str();
		for (int i = ((int)size() - 2); i >= 0; --i) {
			if (path[i] == L'/' || path[i] == L'\\') {
				std::wstring result = &path[i + 1];
				if (IsDir())
					result.pop_back();
				return result;
			}
		}

		return *this;
	}

	Path Parent() const {
		WCHAR path[MAX_PATH + 1] = {};
		wcsncpy_s(path, c_str(), MAX_PATH);
		size_t pathLen = wcslen(path);

		for (int i = ((int)pathLen - 1); i >= 0; --i) {
			if (path[i] == L'/' || path[i] == L'\\')
				path[i] = L'\0';
			else
				break;
		}

		for (size_t i = 0; i < pathLen; ++i) {
			if (path[i] == L'/')
				path[i] = L'\\';
		}

		PathRemoveFileSpec(path);
		return path;
	}

	BOOL IsExists() const {
		return PathFileExists(c_str());
	}
};

class FileCopier {
public:
	FileCopier(Path fromDir, Path toDir) :
		m_fromDir(fromDir), m_toDir(toDir) {
	}

	BOOL Copy(Path subPath) {
		Path to = (m_toDir / subPath).Parent();
		if (!to.MakeDir())
			return FALSE;

		Path from = m_fromDir / subPath;
		return CopyFileOrFolder(from, to);
	}

	BOOL Copy(Path subPath, PCWSTR newName) {
		Path to = (m_toDir / subPath).Parent() / newName;
		if (to.IsExists())
			return FALSE;

		Path from = m_fromDir / subPath;
		return CopyFileOrFolder(from, to);
	}

private:
	static BOOL CopyFileOrFolder(
			Path from, Path to, BOOL errorUI = TRUE) {
		if (!from.IsExists())
			return FALSE;

		from.push_back(NULL);
		to.push_back(NULL);
		SHFILEOPSTRUCT shfo = {
			NULL,
			FO_COPY,
			from.c_str(),
			to.c_str(),
			FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION,
			FALSE,
			NULL,
			NULL
		};

		BOOL result = (SHFileOperation(&shfo) == 0);
		if (!result && errorUI) {
			ErrorMsg(L"Failed to copy: %s => %s", from.c_str(), to.c_str());
		}

		return result;
	}

	Path m_fromDir;
	Path m_toDir;
};

Path GetSelfExePath() {
	WCHAR path[MAX_PATH] = { 0 };
	HMODULE hModule = GetModuleHandle(NULL);
	if (hModule)
		GetModuleFileName(hModule, path, ARRAYSIZE(path));

	return path;
}

class SelfAttachedFiles {
	typedef std::fstream S;

	// offset[4] + length[4] + checksum[4]
	static const size_t META_DATA_NUM = 3;

public:
	bool Init() {
		Path path = GetSelfExePath();
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

	bool ExtractHostTo(int payload_num, const Path& path) {
		int host_size = GetHostSize(payload_num);
		if (host_size <= 0)
			return false;

		ExtractFile(0, host_size, path);
		return true;
	}

private:
	int GetHostSize(int payload_num) {
		extracted_len = 0;

		for (int i = 0; i < payload_num; ++i) {
			DWORD32 offset = 0, length = 0;
			bool result = ReadBackItemInfo(&offset, &length);
			if (!result)
				return 0;
		}

		self_file_.seekg(0, S::end);
		return (int)self_file_.tellg() - extracted_len;
	}

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
		return length && (*offset ^ *length) == checksum;
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

BOOL ExecAndWait(PCWSTR exeFile, PCWSTR args) {
	SHELLEXECUTEINFO ShExecInfo = { 0 };
	ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
	ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	ShExecInfo.hwnd = NULL;
	ShExecInfo.lpVerb = NULL;
	ShExecInfo.lpFile = exeFile;
	ShExecInfo.lpParameters = args;
	ShExecInfo.lpDirectory = NULL;
	ShExecInfo.nShow = SW_SHOW;
	ShExecInfo.hInstApp = NULL;
	ShellExecuteEx(&ShExecInfo);

	if (!ShExecInfo.hProcess) {
		ErrorMsg(L"Failed to run: %s %s", exeFile, args);
		return FALSE;
	}

	WaitForSingleObject(ShExecInfo.hProcess, INFINITE);

	DWORD exitCode = 0;
	BOOL result = GetExitCodeProcess(ShExecInfo.hProcess, &exitCode);
	CloseHandle(ShExecInfo.hProcess);
	return (BOOL)(result && !exitCode);
}

BOOL RemoveDir(Path path, BOOL errorUI = TRUE) {
	if (!path.IsExists())
		return TRUE;

	path.push_back(NULL);
	SHFILEOPSTRUCT shfo = {
		NULL,
		FO_DELETE,
		path.c_str(),
		NULL,
		FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION,
		FALSE,
		NULL,
		NULL
	};

	BOOL result = (SHFileOperation(&shfo) == 0);
	if (!result && errorUI) {
		ErrorMsg(L"Failed to remove: %s", path.c_str());
	}

	return result;
}

BOOL CALLBACK DeleteTrayIcon(HWND window, LPARAM param) {
	WCHAR title[MAX_PATH] = {};
	GetWindowTextW(window, title, MAX_PATH);

	if (wcsstr(title, APP_UID) && wcsstr(title, L"trayicon")) {
		NOTIFYICONDATA nid;
		ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
		nid.hWnd = window;
		nid.uID = 0;
		Shell_NotifyIcon(NIM_DELETE, &nid);
		return FALSE;
	}

	return TRUE;
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
	DWORD pid = entry.th32ProcessID;
	if (!pid || pid == GetCurrentProcessId())
		return;

	HANDLE hProcess = OpenProcess(
		PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
		FALSE, pid);

	if (!hProcess)
		return;

	WCHAR filePath[MAX_PATH] = { 0 };
	GetProcessImageFileName(hProcess, filePath, MAX_PATH);

	if (wcsstr(filePath, APP_UID)) {
		TerminateProcess(hProcess, 0);
	}

	CloseHandle(hProcess);
}

BOOL RemoveAndCreateFolder(PCWSTR path) {
	if (!RemoveDir(path))
		return FALSE;

	CreateDirectory(path, NULL);
	return TRUE;
}

void _GetTempDirPath(PWSTR path, DWORD size) {
	DWORD length = GetTempPath(size, path);
	wsprintf(path + length, APP_UID);
}

Path GetTempDirPath() {
	WCHAR path[MAX_PATH + 1] = { 0 };
	_GetTempDirPath(path, MAX_PATH);
	return path;
}

void _GetAppDirPath(PWSTR path, DWORD size) {
	if (size < MAX_PATH) {
		ErrorMsg(L"GetAppDirPath: size < MAX_PATH");
		return;
	}

	SHGetSpecialFolderPath(
		NULL, path, CSIDL_LOCAL_APPDATA, TRUE);
	wsprintf(path + wcslen(path), L"\\%s", APP_UID);
}

Path GetAppDirPath() {
	WCHAR path[MAX_PATH + 1] = { 0 };
	_GetAppDirPath(path, MAX_PATH);
	return path;
}

void SelfExtractAndExec(Path tempPath, Path appPath, BOOL isUpgrade) {
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
	python_dir.MakeDir();

	if (!Unzip(python_zip, python_dir))
		return;

	if (!Unzip(app_zip, appPath))
		return;

	if (isUpgrade)
		FileCopier(tempPath, appPath).Copy(L"data.old");

	CopyFile(GetSelfExePath(), appPath / L"installer.exe", TRUE);
	BOOL result = ExecAndWait(
		python_dir / L"pythonw.exe", appPath / L"install.py");

	if (result)
		MsgBox(APP_NAME L"has been installed successfully!",
			MB_ICONINFORMATION);
}

VOID BackupUserConf(Path appPath, Path tempPath) {
	FileCopier fc(appPath / L"data", tempPath / L"data.old");
	fc.Copy(L"conf/user");
	fc.Copy(L"html/version.json");
}

VOID RunUnistallScript(Path appPath) {
	Path pyw = appPath / L"python/pythonw.exe";
	if (!pyw.IsExists())
		return;

	Path ver = appPath / L"data/html/version.json";
	if (!ver.IsExists())
		return;

	std::wstring installScript = appPath / L"install.py";
	std::wstring param = installScript + L" " + L"--undo";
	ExecAndWait(pyw, param.c_str());
}

BOOL StopAndUninstall(Path appPath) {
	if (!appPath.IsExists())
		return TRUE;

	RunUnistallScript(appPath);
	EnumWindows(&DeleteTrayIcon, NULL);
	EnumProcess(&KillOldProcesses);
	return RemoveDir(appPath);
}

BOOL InstallOrUpgrade() {
	Path tempPath = GetTempDirPath();
	if (!RemoveAndCreateFolder(tempPath))
		return FALSE;

	Path appPath = GetAppDirPath();
	BOOL isReplacingOldApp = appPath.IsExists();

	if (isReplacingOldApp) {
		BackupUserConf(appPath, tempPath);
		if (!StopAndUninstall(appPath))
			return FALSE;
	}

	if (!RemoveAndCreateFolder(appPath))
		return FALSE;

	SelfExtractAndExec(tempPath, appPath, isReplacingOldApp);
	return TRUE;
}

BOOL IsAnotherInstanceRunning() {
	HANDLE mutex = CreateMutex(NULL, TRUE, APP_UID);
	return mutex && GetLastError() == ERROR_ALREADY_EXISTS;
}

void InstallOrUpgradeRoutine() {
	InstallOrUpgrade();
}

int InstallOrUpgradeUI(HINSTANCE hInstance) {
	std::wstring question = L"Do you want to install " APP_NAME;
	question += APP_VER;
	question += L" now?";

	if (IDYES != MsgBox(question.c_str(), MB_YESNO))
		return ERROR_HANDLE_EOF;

	return StartWaiting(hInstance, &InstallOrUpgradeRoutine);
}

int PackFileUI(PCWSTR newAttach, PCWSTR output) {
	SelfAttachedFiles saf;
	if (!saf.Init())
		return ERROR_OPEN_FAILED;

	if (!saf.PushBackTo(newAttach, output))
		return ERROR_INVALID_PARAMETER;

	return ERROR_SUCCESS;
}

BOOL WaitAnotherQuit(int times) {
	for (int i = 0; i < times; ++i) {
		if (!IsAnotherInstanceRunning())
			return TRUE;

		Sleep(1000);
	}

	return FALSE;
}

int UninstallUI() {
	PCWSTR question = L"Do you want to remove " APP_NAME L"?";
	if (IDYES != MsgBox(question, MB_YESNO))
		return ERROR_HANDLE_EOF;

	if (!WaitAnotherQuit(5))
		return ERROR_NOT_OWNER;

	Path appPath = GetAppDirPath();
	if (!StopAndUninstall(appPath))
		return ERROR_CURRENT_DIRECTORY;

	MsgBox(APP_NAME L" has been removed.", MB_ICONINFORMATION);
	return ERROR_SUCCESS;
}

int CopyUninstall() {
	Path tempPath = GetTempDirPath();
	if (!RemoveAndCreateFolder(tempPath))
		return GetLastError();

	SelfAttachedFiles saf;
	if (!saf.Init())
		return ERROR_OPEN_FAILED;

	int payloadNum = IS_DEBUG ? 0 : 2;
	Path copyPath = tempPath / L"installer.exe";
	saf.ExtractHostTo(payloadNum, copyPath);

	ShellExecute(NULL, NULL, copyPath, L"uninstall", NULL, 0);
	return ERROR_SUCCESS;
}

class Args {
public:
	Args() {
		std::wstring cmd(GetCommandLineW());
#ifdef _DEBUG
		if (strlen(DEBUG_ARGS)) {
			if (*cmd.rbegin() != L' ')
				cmd.append(L" ");
			cmd.append(u8to16(DEBUG_ARGS));
		}
#endif
		m_argList = CommandLineToArgvW(cmd.c_str(), &m_argNum);
	}

	~Args() {
		if (m_argList) {
			LocalFree(m_argList);
			m_argList = NULL;
		}
	}

	LPWSTR Pop() {
		if (!m_argList)
			return L"";

		int index = m_curArg + 1;
		if (index < m_argNum) {
			m_curArg = index;
			return m_argList[index];
		}

		return L"";
	}

	operator bool() {
		return m_argList && m_result;
	}

	Args* operator ->() {
		Reset();
		return this;
	}

	Args& Reset() {
		m_curArg = 0;
		m_result = true;
		return *this;
	}

	Args& PopEquals(LPWSTR str) {
		if (m_result)
			m_result = (0 == wcscmp(Pop(), str));
		return *this;
	}

	Args& Left(int num) {
		if (m_result) {
			int realLeft = (m_argNum - m_curArg - 1);
			m_result = (realLeft == num);
		}
		return *this;
	}

private:
	bool m_result = true;
	int m_curArg = 0;
	int m_argNum = 0;
	LPWSTR* m_argList = NULL;
};

enum class SubCommand {
	Unknown,
	PackFile,
	Upgrade,
	Uninstall,
	CopyUninstall,
};

#pragma warning(push)
#pragma warning(disable: 28251)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
		LPSTR lpCmdLine, int nShowCmd) {
#pragma warning(pop)
	g_msgBoxTitle = APP_NAME;

	Args args;
	SubCommand sc = SubCommand::Unknown;
	if (args->PopEquals(L"push").Left(2))
		sc = SubCommand::PackFile;
	else if (args->Left(0))
		sc = SubCommand::Upgrade;
	else if (args->PopEquals(L"uninstall"))
		sc = SubCommand::Uninstall;
	else if (args->PopEquals(L"copy-uninstall"))
		sc = SubCommand::CopyUninstall;

	if (sc != SubCommand::Uninstall)
		if (IsAnotherInstanceRunning())
			return ERROR_NOT_OWNER;

	if (sc == SubCommand::PackFile) {
		PCWSTR newAttach = args.Pop();
		PCWSTR output = args.Pop();
		return PackFileUI(newAttach, output);
	}
	else if (sc == SubCommand::Upgrade)
		return InstallOrUpgradeUI(hInstance);
	else if (sc == SubCommand::Uninstall)
		return UninstallUI();
	else if (sc == SubCommand::CopyUninstall)
		return CopyUninstall();
	else {
		return ERROR_INVALID_PARAMETER;
	}
}
