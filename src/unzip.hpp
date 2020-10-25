#pragma once
#include <Shldisp.h>

inline void ErrorMsg(PCWSTR format, ...) {
	WCHAR text[1024] = { 0 };
	va_list args;
	va_start(args, format);
	wvsprintf(text, format, args);
	MessageBox(NULL, text, L"Error", MB_ICONERROR);
	va_end(args);
}

template <typename T>
class AutoRelease {
public:
	AutoRelease() : obj_(NULL) {}

	~AutoRelease() {
		if (obj_) {
			obj_->Release();
			obj_ = NULL;
		}
	}

	T* operator-> () {
		return obj_;
	}

	operator bool() {
		return obj_;
	}

	T** Ref() {
		return &obj_;
	}

protected:
	T* obj_;
};

class DispatchVar {
public:
	void Set(IDispatch* dispatch) {
		*dispatch_.Ref() = dispatch;
		var_.vt = VT_DISPATCH;
		var_.pdispVal = dispatch;
	}

	operator VARIANT() {
		return var_;
	}

private:
	AutoRelease<IDispatch> dispatch_;
	VARIANT var_ = {};
};

class AutoFolderItems : public AutoRelease<FolderItems> {
public:
	void GetDispatch(DispatchVar* var) {
		IDispatch* pDispatch = NULL;
		obj_->QueryInterface(IID_IDispatch, (void**)&pDispatch);
		var->Set(pDispatch);
	}
};

class StrVar {
public:
	StrVar(PCWSTR str) : bstr_(NULL) {
		bstr_ = SysAllocString(str);
		var_.vt = VT_BSTR;
		var_.bstrVal = bstr_;
	}

	~StrVar() {
		if (bstr_) {
			SysFreeString(bstr_);
			bstr_ = NULL;
		}
	}

	operator VARIANT() {
		return var_;
	}

private:
	BSTR bstr_;
	VARIANT var_;
};

class Int32Var {
public:
	Int32Var(LONG value) {
		var_.vt = VT_I4;
		var_.lVal = value;
	}

	operator VARIANT() {
		return var_;
	}

private:
	VARIANT var_;
};

inline BOOL CopyFolderItemsImpl(PCWSTR srcFolder, PCWSTR dstFolder) {

	AutoRelease<IShellDispatch> pISD;
	if (CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER,
		IID_IShellDispatch, (void**)pISD.Ref()) != S_OK)
		return FALSE;

	AutoRelease<Folder> pZippedFile;
	pISD->NameSpace(StrVar(srcFolder), pZippedFile.Ref());
	if (!pZippedFile)
		return FALSE;

	AutoRelease<Folder> pDestination;
	pISD->NameSpace(StrVar(dstFolder), pDestination.Ref());
	if (!pDestination)
		return FALSE;

	AutoFolderItems pFilesInside;
	pZippedFile->Items(pFilesInside.Ref());
	if (!pFilesInside)
		return FALSE;

	long filesCount = 0;
	pFilesInside->get_Count(&filesCount);
	if (filesCount < 1)
		return TRUE;

	DispatchVar items;
	pFilesInside.GetDispatch(&items);

	// https://docs.microsoft.com/en-us/windows/win32/shell/folder-copyhere
	Int32Var options(1024 | 512 | 16 | 4);
	return pDestination->CopyHere(items, options) == S_OK;
}

inline BOOL CopyFolderItems(PCWSTR srcFolder, PCWSTR dstFolder) {
	CoInitialize(NULL);
	BOOL result = false;
	__try {
		result = CopyFolderItemsImpl(srcFolder, dstFolder);
	}
	__finally {
		CoUninitialize();
	}

	return result;
}

inline BOOL Unzip(PCWSTR srcZipFile, PCWSTR dstFolder) {
	// NOTE: srcZipFile must end with ".zip" suffix
	BOOL result = CopyFolderItems(srcZipFile, dstFolder);
	if (!result)
		ErrorMsg(L"Failed to unzip \"%s\" to \"%s\".",
			srcZipFile, dstFolder);

	return result;
}
