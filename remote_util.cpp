/*
* @Author: iJiabao <ijiabao@qq.com>
* @Home: https://github.com/ijiabao
*/

#include "remote_util.h"

#define NTHEADER(img) (IMAGE_NT_HEADERS*)((UINT_PTR)img + ((IMAGE_DOS_HEADER*)img)->e_lfanew)

// ע���Dll�׵�ַ����ΪHMODULE�����ǲ��ɵ���HMODULE����
static void* __image = 0;
static ModuleExtData* __extdata = 0;	// ��������


// �޸������
int __stdcall FixupImports(void* image);

// ǿ��֢���ߵĸ���, �ͷ�����
int __declspec(naked) __stdcall SafeExit(int CodeExit = -8888){
	__asm{
		//push -8888				// ����ExitThread����, ���˳�ֵ.
		PUSH DWORD PTR [ESP + 4];	// ����ExitThread����, ���˳�ֵ.(��ʱ����Ϊ����Ĳ���CodeExit) ;
		PUSH DWORD PTR PostQuitMessage;	// ExitThreadִ�к󷵻صĵط�, ��ʵ����ԶҲ���᷵��
		PUSH MEM_RELEASE			// Virtual Free ����������
		PUSH 0
		PUSH DWORD PTR __image;
		PUSH DWORD PTR ExitThread	// ִ��VirautlFree��, ���أ����ã�ExitThread
		JMP DWORD PTR VirtualFree	// �Ͱ汾��VS, dword ptr �Ǳ����
		retn
	}
}

// ��Ϊע��DLL���˺���������ڵ㣬Ҳ��ΪRemoteThread���赼��������ע��˵��� CreateRemoteThread ��ִ�д˺���
// �˺���ִ�й�����LoadLibrary��ĳ�ʼ���ǻ���һ�µ�
#if defined(_WIN64)
#pragma comment(linker, "/EXPORT:RemoteCRTStartup=?RemoteCRTStartup@@YAKPEAX@Z,@888") // @@YGKPAX@Z,@8,NONAME
#else
#pragma comment(linker, "/EXPORT:RemoteCRTStartup=?RemoteCRTStartup@@YGKPAX@Z,@888") // @@YGKPAX@Z,@8,NONAME
#endif

/*__declspec(dllexport)*/ // �������linker��ָ���˺����ĵ�����������
DWORD WINAPI RemoteCRTStartup(LPVOID lpThreadParameter){
	// ��������Ϊע��module���׵�ַ
	__image = lpThreadParameter;
	if(!FixupImports(__image)){
		return SafeExit();
	}

	PIMAGE_NT_HEADERS nth = NTHEADER(__image);
	// ��������
	__extdata = (ModuleExtData*) ((char*)__image + nth->OptionalHeader.SizeOfImage);

	// ԭʼDll��ڵ�
	BOOL(APIENTRY* DllMainCRTStartup)(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) = 0;
	*(UINT_PTR*)& DllMainCRTStartup = (UINT_PTR) __image + nth->OptionalHeader.AddressOfEntryPoint;
	
	// ����ԭʼ��ڵ�,��ʼ��CRT. (����ɹ�,���Զ�����DllMain(), ���ǵ�����������DllMain������)
	DllMainCRTStartup((HMODULE)__image, DLL_PROCESS_ATTACH,NULL);
	
	//TestCRT(0);
	//�������,����DLL_PROCESS_DETACH
	DllMainCRTStartup((HMODULE)__image, DLL_PROCESS_DETACH,NULL);
	return SafeExit();
}


// �޸����뺯����ַ��,����ע�Ͳμ�PELoader::FixupImports
int __stdcall FixupImports(void* image){
	if (!image){
		MessageBox(NULL, L"��������!", L"��ʾ��Ϣ", MB_OK);
		return 0;
	}

	char* module = (char*)image;	// ���ڼ���RVA
	
	// �������Ϣ
	PIMAGE_NT_HEADERS nth = NTHEADER(module);
	IMAGE_DATA_DIRECTORY dir = nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.Size) return 1;
	
	// �������α�
	PIMAGE_IMPORT_DESCRIPTOR desc = (PIMAGE_IMPORT_DESCRIPTOR) (module + dir.VirtualAddress);

	while (desc->Characteristics){	// 0 for terminating null import descriptor
		char* dllname = module + desc->Name;
		PIMAGE_THUNK_DATA orig = (PIMAGE_THUNK_DATA)(module + desc->OriginalFirstThunk);
		PIMAGE_THUNK_DATA iat = (PIMAGE_THUNK_DATA) (module + desc->FirstThunk);
		
		HMODULE dll = GetModuleHandleA(dllname);
		if (!dll) dll = LoadLibraryA(dllname);
		if (!dll) {
			MessageBoxA(0, dllname, 0, 0);
			MessageBox(0, L"����ģ��ʱ,�޸������ʧ��!", L"��ʾ��Ϣ", MB_OK);
			return 0;
		}
		// ���λ�ȡ������(���)��д���ӦIAT��ַ
		while (orig->u1.Function){
			char* func = 0;
			if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)){ //��� (���λ��1)
				func = (char*)IMAGE_ORDINAL(orig->u1.Ordinal);	// ȡ��λ(�������)
			}
			else{ // ���� ��ȡRVA, ΪIMAGE_IMPORT_BY_NAMEָ��)
				char* addr = module + (UINT_PTR) orig->u1.AddressOfData;
				func = (char*) ((PIMAGE_IMPORT_BY_NAME)addr)->Name;
			}
			*(FARPROC*)& (iat->u1.Function) = GetProcAddress(dll, func);
			orig++; iat++;
		}

		desc++;
	}
	return 1;
}

// ��������
ModuleExtData* GetModuleExtData(){
	return __extdata;
}


// ��־���
#include <io.h>
#include <fcntl.h>
extern "C"{
WINBASEAPI
HWND
APIENTRY
GetConsoleWindow(
    VOID
    );

WINUSERAPI
BOOL
WINAPI
SetLayeredWindowAttributes(
    __in HWND hwnd,
    __in COLORREF crKey,
    __in BYTE bAlpha,
    __in DWORD dwFlags);
};

// ��־��ȫ��
namespace Log 
{

static FILE* __logfile = 0;

int __cdecl out(const wchar_t* format, ...){
	int result = 0;
	va_list ap;
	va_start(ap, format);
	result += vfwprintf(__logfile, format, ap);
	va_end(ap);
	return result;
}

int __cdecl outA(const char* format, ...){
	int result = 0;
	va_list ap;
	va_start(ap, format);
	result += vfprintf(__logfile, format, ap);
	va_end(ap);
	return result;
}


int __stdcall SetConsoleWindow(){
	HWND _hConsole = GetConsoleWindow();
	// ��ֹ�ر�
	EnableMenuItem(::GetSystemMenu(_hConsole, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
	
	// �Ƶ����Ͻ�
	int width = GetSystemMetrics(SM_CXSCREEN);
	int height = GetSystemMetrics(SM_CYSCREEN);
	RECT rc;
	GetWindowRect(_hConsole, &rc);
	int w = rc.right - rc.left; int h = rc.bottom - rc.top;
	MoveWindow(_hConsole, width - w, 0, w, h, TRUE);
	
	// �޸�͸����
	DWORD _WS_EX_LAYERED = 0x00080000;
	DWORD _LWA_ALPHA = 0x00000002;
	DWORD style = GetWindowLong(_hConsole, GWL_EXSTYLE);
	SetWindowLong(_hConsole, GWL_EXSTYLE, style | _WS_EX_LAYERED);
	SetLayeredWindowAttributes(_hConsole, 0, (255 * 70) / 100, _LWA_ALPHA);
	return 1;
}
	
int __stdcall Init(const wchar_t* file){
	if (file){
		wchar_t path[MAX_PATH];
		swprintf(path, L"%ls\\%ls", GetModuleExtData()->loader_dir, file);
		__logfile = _wfopen(path, L"w+t");
	}
	else{
		AllocConsole();
		SetConsoleWindow();
		HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
		int fd = _open_osfhandle((intptr_t)hOut, _O_TEXT);
		__logfile = _wfdopen(fd, L"w+t");
	}
	if (!__logfile){
		return 0;
	}
	setvbuf(__logfile, NULL, _IONBF, 0);
	_wsetlocale(LC_ALL, L"");
	
	out(L"Զ���ն˲��԰�V1.0\n\tPowered by ijiabao<ijiabao@qq.com>\n\tstdout:%d\n", _fileno(stdout));
	return 0;
}

int __stdcall Free()
{
	if (GetConsoleWindow()){
		_wsystem(L"pause");
		FreeConsole();	// ���Զ��ر�osf_handle
		__logfile = 0;	// �����ٴ�fclose���׳��쳣
	}
	if (__logfile){
		fclose(__logfile);
		__logfile = 0;
	}
	
	return 1;
}


};



// ����Զ���նˣ�������
ConsoleHlp::ConsoleHlp(){
	AllocConsole();
	_wfreopen(L"CONOUT$", L"w+t", stdout);
	setvbuf(stdout, NULL, _IONBF, 0);
	_wsetlocale(LC_ALL, L"");
	
	wprintf(L"Զ���ն˲��԰�V1.0\n\tPowered by ijiabao<ijiabao@qq.com>\n\tstdout:%d\n", _fileno(stdout));
	
	_hConsole = GetConsoleWindow();
	
	// ��ֹ�ر�
	//EnableMenuItem(::GetSystemMenu(_hConsole, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
	
	// �Ƶ����Ͻ�
	int width = GetSystemMetrics(SM_CXSCREEN);
	int height = GetSystemMetrics(SM_CYSCREEN);
	RECT rc;
	GetWindowRect(_hConsole, &rc);
	int w = rc.right - rc.left; int h = rc.bottom - rc.top;
	MoveWindow(_hConsole, width - w, 0, w, h, TRUE);
	
	// �޸�͸����
	DWORD _WS_EX_LAYERED = 0x00080000;
	DWORD _LWA_ALPHA = 0x00000002;
	DWORD style = GetWindowLong(_hConsole, GWL_EXSTYLE);
	SetWindowLong(_hConsole, GWL_EXSTYLE, style | _WS_EX_LAYERED);
	SetLayeredWindowAttributes(_hConsole, 0, (255 * 70) / 100, _LWA_ALPHA);
	
}
ConsoleHlp::~ConsoleHlp(){
	_wsystem(L"pause");
	fclose(stdout);
	FreeConsole();
}


void ConsoleHlp::reopen_std(){
	// ԭʼfileno�п�������Ч��,Ҳ�������ϴ��ѹرյ�
	_out = (_fileno(stdout) == 1) ? _dup(1) : -1;
	//_in = (_fileno(stdin) == 0) ? _dup(0) : -1;
	
	_wfreopen(L"CONOUT$", L"w+t", stdout);
	//_wfreopen(L"CONIN$", L"r+t", stdin);
	
	// ��Ϊ�ֶ��ض�����ϵͳ�������һ��(��ǰ���ļ���)
	// ����ʲô�������ض���stdin֮��VC8�л�������
	if (0){
		HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
		int fdout = _open_osfhandle((intptr_t)hout, _O_TEXT);
		if (_out < 0){ // ��Чstdout, ֱ�ӿ���������رգ����軹ԭ
			FILE* fp = _wfdopen(fdout, L"w+t");
			*stdout = *fp;
		}
		else{
			_dup2(fdout, 1); _close(fdout);
		}
	}
	if (0) {	
		HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
		int fdin = _open_osfhandle((intptr_t)hin, _O_TEXT);
		if (_in < 0){
			FILE* fp = _wfdopen(fdin, L"r+t");
			*stdin = *fp;
		}
		else{
			_dup2(fdin, 0); _close(fdin);
		}
	}
	
	setvbuf(stdout, NULL, _IONBF, 0);
	_wsetlocale(LC_ALL, L"");
	
	// wprintf(L"bakup std=%d %d, reopened std=%d %d\n", _out, _in, _fileno(stdout), _fileno(stdin));
}

// �ָ��ض���, ����VC��ͬʱע����ԣ������쳣
void ConsoleHlp::restory_std(){
	if (_out < 0){	// ԭ��stdout��Ч,��ʱstdoutΪ�´򿪵ģ�vc8����ر�)
		fclose(stdout);
	}
	else {	// ԭ��stdoutΪ��׼����������ʹ��fclose�����´��ض���ʱstdout�ļ��Ŷ�ʧ
		_close(1); _dup2(_out, 1); _close(_out);
	}
	
	return;
	if (_in < 0){
		fclose(stdin);
	}
	else{
		_close(0); _dup2(_in, 0); _close(_in);
	}
}

// ����һ���򵥹���Ӧ��
SingleHooker::SingleHooker()
{
	_addr = 0; _val = 0; _orig = 0;
}

SingleHooker::~SingleHooker()
{
	Stop();
}

// ��������
int __stdcall SingleHooker::Start()
{
	if (!_orig && _addr && _val){
		outlog(L"��������!\n");
		return EditMemory(_addr, _val, &_orig);
	}
	return 1;	
}

// ֹͣ����
int __stdcall SingleHooker::Stop()
{
	if (_orig && _addr){
		outlog(L"ֹͣ����!\n");
		EditMemory(_addr, _orig);
		_orig = 0;
	}
	return 1;
}

// ���麯��
int __stdcall SingleHooker::SetVTable(void* object, int offset, void* func, void** orig_func)
{
	_addr = *(char**)object + offset;
	_val = func;
	*orig_func = *(void**)_addr;
	return 1;
}

// ��EIP�� ��֧��ff15, e8, f8 ����Call
int __stdcall SingleHooker::SetEip(void* eip, void* func, void** orig_func)
{
	if (_orig) return 0;	// ���hook�ˣ�����orig����֮��մ�ֵ
	// ������eip�ÿɶ�
	DWORD protect = PAGE_EXECUTE_READ, bak_protect = 0;
	if (!VirtualProtect(eip, sizeof(void*) * 2, protect, &bak_protect)){
		return 0;
	}

	if (*(WORD*)eip == 0x15ff){	// ff15 op => call dword ptr [op]
		_addr = (char*)eip + 2;			// Ҫ�޸ĵĵ�ַ
		*orig_func = **(void***)_addr;	// ȡһ��Ϊ������(op)����ȡһ��Ϊ������ַ
		static void* tmp; tmp = func;	// �µ�ַҲ��Ϊ���Ѱַ, ��_valָ��ȡֵ���Ǻ�����ַ��
		_val = &tmp;
	}
	else if (*(BYTE*)eip == 0xe8){	// e8 op = call (op + next_eip)
		_addr = (char*)eip + 1;
		UINT_PTR next_eip = (UINT_PTR)_addr + sizeof(void*);
		*(UINT_PTR*)orig_func = *(UINT_PTR*)_addr + next_eip;	// ������ַΪ: ������ + NextEIP
		*(UINT_PTR*)& _val = (UINT_PTR)func - next_eip;			// �²�����Ϊ��������ַ - NextEIP
	}
	else if (*(BYTE*)eip == 0xf8){	// f8 op = call op
		_addr = (char*)eip + 1;
		*orig_func = *(void**)_addr;	// ��������Ϊ������ַ
		_val = func;
	}
	return 1;
}

// д�ڴ�
int __stdcall SingleHooker::EditMemory(void* addr, void* val, void** orig)
{
	DWORD protect = PAGE_EXECUTE_READWRITE, bak_protect = 0;
	if (VirtualProtect(addr, 4, protect, &bak_protect)){
		if (orig) *orig = *(void**)addr;
		*(void**)addr = val;
		return VirtualProtect(addr, 4, bak_protect, &protect);
	}
	return 0;
}

