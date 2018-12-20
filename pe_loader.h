/*
* @Author: iJiabao <ijiabao@qq.com>
* @Home: https://github.com/ijiabao
*/

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#ifndef __LOG_H__
	#include <iostream>
	#define outlog wprintf
	#define outlogA printf
#endif

#define RVA(m,b)	((UINT_PTR)(m) + (UINT_PTR)(b))
#define NTHEADER(img)	((IMAGE_NT_HEADERS*) RVA(img, ((IMAGE_DOS_HEADER*)img)->e_lfanew))
typedef BOOL (APIENTRY* DllMainCRTStartupProc)(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved);

class PELoader{
protected:
	void* _module;

	void* _extdata;
	int _extdata_size;
public:
	PELoader();
	virtual ~PELoader();

	int __stdcall CreateImage(const wchar_t* filename);
	int __stdcall FixupImports();						// �޸������
	int __stdcall FixupRelocations(void* real_base=0);	// �ض���
	int __stdcall HackHeader();							// ĨȥPE��Ϣ
	int __stdcall Release();
	
	FARPROC __stdcall GetProcAddr(char* name);				// ��ȡ(����)������ַ
	static int __stdcall ValidNTHeader(const void* header);	// ��֤PEͷ��ȷ��

	int __stdcall RunDllMainCRTStartup(						// ��������
		DWORD ul_reason_for_call = DLL_PROCESS_ATTACH, 
		LPVOID lpReserved = NULL
	);

	// ����ģ�鸽������
	void __stdcall SetExtData(void* data, int size) { _extdata = data; _extdata_size = size; }
	// ע��Զ��
	void* __stdcall Inject(HANDLE hProcess, char* StartFunc, void** ThreadMain);
	// �ͷ�Զ�̿ռ� (ʾ��DLL�и���ɾ���������ͷ�)
	static int __stdcall UnInject(HANDLE hProcess, void* remote){
		return VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
	}

	
	// �����ض�λ,ȡ��ϵͳAPI
	static PIMAGE_BASE_RELOCATION APIENTRY MyProcessRelocationBlock(PVOID Page, DWORD Count, PUSHORT TypeOffset, UINT_PTR Delta);
	
public:
	 static int __stdcall EnableDebugPrivileges();
	 static int __stdcall IsElevatedToken();
	 static int __stdcall GetCurrentModuleDir(wchar_t* buffer, int max_buffer = MAX_PATH);
	 static int __stdcall SetCurrentExecDir();
	 static int __stdcall RunasAdmin(const wchar_t* params);
	 static int __stdcall TestInject(HWND hwnd, wchar_t* dllfile, int wait = TRUE);
};