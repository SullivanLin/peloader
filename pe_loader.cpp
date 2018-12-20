/*
* @Author: iJiabao <ijiabao@qq.com>
* @Home: https://github.com/ijiabao
*/

#include "pe_loader.h"
#include <shellapi.h>

#pragma warning (disable:4996)
#pragma warning (disable:4312)

PELoader::PELoader(){
	_module = 0;
	_extdata = 0;
	_extdata_size = 0;
}

PELoader::~PELoader(){
	Release();
}
// �ͷ��ڴ�
int __stdcall PELoader::Release(){
	if(_module){
		int result = VirtualFree(_module, 0, MEM_RELEASE);
		_module = NULL;
		outlog(result ? L"PE�ڴ��ͷ�ʧ�ܣ�\n" : L"PE�ڴ��ͷųɹ���\n");
	}
	_extdata = 0; _extdata_size = 0;
	return 1;
}

// ����֤PEͷ��Ϣ
int __stdcall PELoader::ValidNTHeader(const void* header){
	if (((IMAGE_DOS_HEADER*)header)->e_magic != IMAGE_DOS_SIGNATURE){
		outlog(L"��Ч��PE���ݣ�\n");
		return 0;
	}
	IMAGE_NT_HEADERS* nth = NTHEADER(header);
	if (nth->Signature != IMAGE_NT_SIGNATURE){
		outlog(L"��Ч��PE���ݣ�\n");
		return 0;
	}
	WORD magic = nth->OptionalHeader.Magic;
	if(IMAGE_NT_OPTIONAL_HDR32_MAGIC == magic && (sizeof(void*) != 4)){	//32λͷ
		outlog(L"Ŀ��PEΪ32λ, �����б������32λ�汾\n");
		return 0;
	}
	if(IMAGE_NT_OPTIONAL_HDR64_MAGIC == magic && sizeof(void*) != 8){
		outlog(L"Ŀ��PEΪ64λ, �����б������32λ�汾\n");
		return 0;
	}
	if(IMAGE_ROM_OPTIONAL_HDR_MAGIC == magic){
		outlog(L"�ǿ�ִ���ļ�\n");
		return 0;
	}
	return 1;
}



int __stdcall PELoader::CreateImage(const wchar_t* filename){
	FILE* fp = _wfopen(filename, L"rb");
	if (!fp) return 0;

	IMAGE_DOS_HEADER dosh;
	IMAGE_NT_HEADERS nth;

	fread(&dosh, sizeof(dosh), 1, fp);
	fseek(fp, dosh.e_lfanew, SEEK_SET);
	fread(&nth, sizeof(nth), 1, fp);
	
	SIZE_T image_size = nth.OptionalHeader.SizeOfImage;
	void* alloc_addr = (void*) nth.OptionalHeader.ImageBase;
	// ������PE�Ĳο���ַ�����ٿռ�,���ɹ�����Ҫ�ض�λ
	void* img = VirtualAlloc(alloc_addr, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!img){
		img = VirtualAlloc(0, image_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	}
	if (!img){
		fclose(fp);
		outlog(L"����ҳ���ڴ�ʧ��!\n");
		return 0;
	}
	outlog(L"�ο���ַΪ:%p\n", (void*)(UINT_PTR)nth.OptionalHeader.ImageBase);
	outlog(L"����DLLӳ���ַΪ��%p, ��СΪ0x%x(%dkb)\n", img, image_size, image_size / 0x1000);


	// ��������ͷ(������Ҫͷ),��ֹ��������
	fseek(fp, 0, SEEK_SET);
	fread(img, nth.OptionalHeader.SizeOfHeaders, 1, fp);
	//memcpy(img, rawdata, nth.OptionalHeader.SizeOfHeaders);
	// ����RAW���ݵ�������, NTͷ��������,�����ž��ǵ�һ��������
	IMAGE_NT_HEADERS* h = NTHEADER(img);
	IMAGE_SECTION_HEADER* seg = IMAGE_FIRST_SECTION(h);
	for (int i = 0; i< nth.FileHeader.NumberOfSections; i++){
		if (!seg->VirtualAddress || !seg->Misc.VirtualSize){
			continue; //�սڣ�
		}
		void* dest = (char*)img + seg->VirtualAddress;
		//void* src = (char*)rawdata + seg->PointerToRawData;

		// sizeRawData, VirtualSize �μ�΢��MSDN http://msdn.microsoft.com/en-us/library/ms680341(v=vs.85).aspx
		DWORD size = seg->Misc.VirtualSize;
		if (size > seg->SizeOfRawData){	// Ŀ��ռ�ÿռ��Դ���ݴ�
			size = seg->SizeOfRawData;	// �������δ��ʼ������
			memset((char*)dest + size, 0, seg->Misc.VirtualSize - size);
		}
		//memmove(page, data, size);
		fseek(fp, seg->PointerToRawData, SEEK_SET);
		fread(dest, size, 1, fp);

		//�����ƴ���8���ַ�ʱ��β����'\0'�ᱻ����.
		char name[9]; name[8] = 0;
		memcpy(name, seg->Name, 8);
		outlogA("�Ѹ��ƣ�����%s\t��С%d(bytes)\n", name, size);
		seg++;
	}
	_module = img;
	//UnloadRaw(rawdata);
	return 1;
}


// �����뺯����, ע���ַ��this->_moduleΪ׼,��NTHEADER(_module)->ImageBase�Ƕ��ٲ��ؿ���
// ���������,���������,��Ҫ������±�:
// IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT [�󶨵����]
// IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT [�ӳٵ����]
// IMAGE_DIRECTORY_ENTRY_IAT �����ַ��, �����iat_thunk����ָ�������ĳ����Ŀ

int __stdcall PELoader::FixupImports(){
	outlog(L"���������...\n");
	
	PIMAGE_NT_HEADERS nth = NTHEADER(_module);
	//UINT_PTR image = nth->OptionalHeader.ImageBase;
	
	UINT_PTR image = (UINT_PTR) _module;
	
	IMAGE_DATA_DIRECTORY dir = nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if(!dir.Size){
		outlog(L"�޵����!\n"); return 0;
	}

	// ���α�
	IMAGE_IMPORT_DESCRIPTOR* import_desc = (IMAGE_IMPORT_DESCRIPTOR*) (image + dir.VirtualAddress);
	
	// ����desc(����)��ÿ��desc��¼��һ��dll���ƣ��Լ���Ӧdll�ĺ������Ʊ�(orig_thunk)��һ��IAT��ַ��(iat_thunk)
	while (import_desc->Characteristics){	// 0 for terminating null import descriptor
		char* dllname = (char*)image + import_desc->Name;	//dll��
		//__outvar(import_desc->TimeDateStamp, "[DWORD]%d");	//ʱ���
		//__outvar(import_desc->ForwarderChain, "[DWORD]%x");	
		IMAGE_THUNK_DATA* orig_thunk = (IMAGE_THUNK_DATA*)	//��������
			(image + import_desc->OriginalFirstThunk);
		IMAGE_THUNK_DATA* iat_thunk = (IMAGE_THUNK_DATA*)	//��Ӧ��IAT��ַ��
			(image + import_desc->FirstThunk);
		//

		//outlogA("%s\n", dllname);
		HMODULE import_dll = 0;
		if (!(import_dll = GetModuleHandleA(dllname))){
			if (!(import_dll = LoadLibraryA(dllname))){
				outlog(L"���������Dllʧ�ܣ�\n");
				return 0;
			}
		}

		//���δ�OriginaThunk�õ������������,��ȡ������ַ��д���Ӧ��IAT��ַ
		while (orig_thunk->u1.Function){
			// ����orig_thunk, ��¼���Ǻ�������(�����)
			char* func_name = 0;
			if (IMAGE_SNAP_BY_ORDINAL(orig_thunk->u1.Ordinal)){		// �������, ���λΪ1
				func_name = (char*) IMAGE_ORDINAL(orig_thunk->u1.Ordinal);	// ȡ��4λ; 64λϵͳΪ��8λ
			}
			else {	//������ (RVA)
				PIMAGE_IMPORT_BY_NAME iibn = (PIMAGE_IMPORT_BY_NAME)
					(image + (UINT_PTR)orig_thunk->u1.AddressOfData); // for vc6 must (UINT_PTR)
				func_name = (char*)(iibn->Name);
			}

			// ����iat_thunk, ��¼����IAT��(�����õ�ַ),��ֱ���޸�.����д������64λϵͳ. iat_thunk == &iat_thunk->u1.Function
			*(FARPROC*)iat_thunk = GetProcAddress(import_dll, func_name);
			
			// ������Ϣ
			//outlogA("\tIAT [%08p]\tAddress [%08p]", iat_thunk, (void*)(UINT_PTR)iat_thunk->u1.Function);

			orig_thunk++;
			iat_thunk++;
		}
		import_desc++;
	}
	return 1;
}

// ����ʵ�ʻ�ַ��nth->ImageBase��ƫ�����������ض�λ��Ŀ���ַĬ��Ϊthis->_module�������󣬿����ڱ���ִ��
int __stdcall PELoader::FixupRelocations(void* real_base){
	
	UINT_PTR image = (UINT_PTR)_module;
	PIMAGE_NT_HEADERS nth = NTHEADER(_module);
	if (!real_base) real_base = _module;
	//ʵ�ʻ�ַ��ο���ַ֮��
	UINT_PTR delta = (UINT_PTR)real_base - (UINT_PTR) nth->OptionalHeader.ImageBase;
	if(0 == delta){
		outlog(L"����Ҫ�ض�λ\n");
		return 1;
	}
	
	// ���ض�λ��ʵ����������û���У���һ��������һ������
	int index = IMAGE_DIRECTORY_ENTRY_BASERELOC;
	if (nth->OptionalHeader.DataDirectory[index].Size <= 0){	//û���ض�λ��?
		return 1;
	}
	outlog(L"�����ض�λ:\n");
	// �ض�λAPI��δ����)�� ����IMAGE_BASE_RELOCATION��¼��������һ����¼
	PIMAGE_BASE_RELOCATION(APIENTRY* LdrProcessRelocationBlock)(PVOID Page, DWORD Count, PUSHORT TypeOffset, UINT_PTR Delta) = 0;
	*(FARPROC*)& LdrProcessRelocationBlock = GetProcAddress(GetModuleHandle(L"NTDLL"), "LdrProcessRelocationBlock");
	if(!LdrProcessRelocationBlock){	//����API
		LdrProcessRelocationBlock = MyProcessRelocationBlock;
	}
	
	// �õ���һ���ض�λ��¼(��)
	PIMAGE_BASE_RELOCATION reloc = (IMAGE_BASE_RELOCATION*)
		(image + nth->OptionalHeader.DataDirectory[index].VirtualAddress);
	// ����ÿ���飬������һ��reloc�ṹ���ݳơ������������������ض�λ����(���飩���μ��˿��struct����
	while (reloc->VirtualAddress){
		// �ض�λ���ڵ�Ŀ��ҳ
		UINT_PTR dest_page = image + reloc->VirtualAddress;
		// ���е�ͬ�� (UINT_PTR)reloc + sizeof(IMAGE_BASE_RELOCATION);
		USHORT *items = (USHORT*)(reloc + 1);
		// ���ݸ��� = ���С-���С / �������ݴ�С
		DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
		// ������Ϣ:
		//char name[9]; memset(name, 0, 9);
		//GetSectionName((void*)dest_page, name);
		//outlogA("ҳַ:%p\t����:%s\t����:%d\t���С:%d\n", dest_page, name, count, reloc->SizeOfBlock);
		// �����ض�λ��¼,������һ��relocָ��
		reloc = LdrProcessRelocationBlock((void*)dest_page, count, items, delta);
		if(!reloc) return 0;
	}
	//����ImageBase
	//*(UINT_PTR*)&nth->OptionalHeader.ImageBase = (UINT_PTR)dest;
	return 1;
}

// �����ض�λ����, ����win2kԴ�룬��ͨ��64λ����
PIMAGE_BASE_RELOCATION APIENTRY PELoader::MyProcessRelocationBlock(PVOID Page, DWORD Count, PUSHORT TypeOffset, UINT_PTR Delta){
	UINT_PTR dest_page = (UINT_PTR)Page;
	PUSHORT items = TypeOffset;
	UINT_PTR delta = Delta;
	// ��֤, ���ַ = TypeOffset - sizeof(IMAGE_BASE_RELOCATION);	//�μ�IMAGE_BASE_RELOCATION����
	PIMAGE_BASE_RELOCATION reloc = (PIMAGE_BASE_RELOCATION)((UINT_PTR)TypeOffset - sizeof(IMAGE_BASE_RELOCATION));
	DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
	if(Count != count) return 0;
	
	// �����Ǵ�Win2KԴ�����ҵ��ģ�������API��ʵ�ֹ��̣����о���������64λ����ͨ���˲���
	for (DWORD i = 0; i<count; i++){
		DWORD item = items[i];
		int offset = item & 0xFFF;	//��12λ, ƫ��ֵ
		int type = item >> 12;		//��4λ, �ض�λ���ͣ��޸ķ�ʽ��

		UINT_PTR dest = dest_page + offset; // ��ָ��Ҫ�޸ĵġ���ַ��
		switch (type){
		case IMAGE_REL_BASED_ABSOLUTE:
			break;
		case IMAGE_REL_BASED_HIGH:
			*(PUSHORT)dest += HIWORD(delta);
			break;
		case IMAGE_REL_BASED_LOW:
			*(PUSHORT)dest += LOWORD(delta);
			break;
		case IMAGE_REL_BASED_HIGHLOW:
			// ����Ŀ�������� A1 ( 0c d4 02 10)  �������ǣ� mov eax , [1002d40c]
			// ��destָ�����ţ�����Ҫ�޸�0x1002d40c���32λ����ַ��
			*(PUINT_PTR)dest += delta;
			break;
		case IMAGE_REL_BASED_DIR64:
			*(PUINT_PTR)dest += delta;
			break;
		case IMAGE_REL_BASED_HIGHADJ:
		case IMAGE_REL_BASED_MIPS_JMPADDR:
		case IMAGE_REL_BASED_MIPS_JMPADDR16:	//IMAGE_REL_BASED_IA64_IMM64
		default:
			//wprintf(L"δ֪/��֧�ֵ��ض�λ���� %hu.\n", type);
			return 0;
		}

	}
	//������һ����¼
	return (PIMAGE_BASE_RELOCATION)((UINT_PTR)reloc + reloc->SizeOfBlock);
	
}


// ��ȡ��������, ��ַ��this->_moduleΪ׼
FARPROC PELoader::GetProcAddr(char* name){
	PIMAGE_NT_HEADERS nth = NTHEADER(_module);
	//UINT_PTR img = _nth->OptionalHeader.ImageBase;
	UINT_PTR img = (UINT_PTR)_module;
	int index = IMAGE_DIRECTORY_ENTRY_EXPORT;
	if (!nth->OptionalHeader.DataDirectory[index].Size){
		return NULL;
	}

	IMAGE_EXPORT_DIRECTORY* ied = (IMAGE_EXPORT_DIRECTORY*)
		(img + nth->OptionalHeader.DataDirectory[index].VirtualAddress);

	// ע:��������ͻ����Ĺ�ϵ:Functions[], Names[], NameOrdinals[], Base
	// ���������ȡַ: ������ַ = Functions[��� - ����];  (�����Ӧ����'���'��Ϊ����,���=���+����)
	// ��������ȡ��ַ: ��Names[N]=������, �����ordinals = NameOrdinals[N] (��ע), ������ַ = Functions[ordinals]
	// �ɼ� NameOrdinals[]����ȡ�õġ���š�ֵ,ʵ�ʾ����±�,��СֵΪ0,�������������ϵĺ����������
	// 		���⻰:֮ǰ�����Ϻܶ�̳���Names[N] �� ���= ordinals[N]+����-1, �ٵ�Function[���], ���е�+����-1������ͷ��,û�κ�����;
	// ͨ��Ĭ�ϻ���Ϊ1, ��ʽ��Ϊ:���=ordinals[N], �Ѳ���������Dllʱָ��������Ϊ1, ���ô˹�ʽ,������Ȼ����, �ӻ����ټ�1,��֪��������;
	// �������ϵĶ���Ҳ��һ������ȷ��,һ��Ҫ������֤��������

	DWORD*	names = (DWORD*)(img + ied->AddressOfNames);
	WORD*	ordinals = (WORD*)(img + ied->AddressOfNameOrdinals);
	DWORD*	funcs = (DWORD*)(img + ied->AddressOfFunctions);

	int ordinal = -1;
	if (HIWORD(name) == 0){ //�������
		ordinal = LOWORD(name) - ied->Base;
		//__outlogA("�������%d\n", ordinal);
	}
	else { // �Ѻ��������õ����
		for (DWORD i = 0; i < ied->NumberOfNames; i++){
			char* curr_name = (char*)(img + names[i]);
			if (strcmp(name, curr_name) == 0){
				//__outlogA("�ҵ�������=%s", curr_name);
				ordinal = ordinals[i];
				break;
			}
		}
	}

	if (ordinal < 0 || (DWORD)ordinal >= ied->NumberOfFunctions){
		return NULL;
	}

	return (FARPROC)(img + funcs[ordinal]);
}

// ����Dll��ڵ�
int __stdcall PELoader::RunDllMainCRTStartup(DWORD ul_reason_for_call, LPVOID lpReserved){
	DllMainCRTStartupProc DllMain = 
		(DllMainCRTStartupProc)RVA(_module, NTHEADER(_module)->OptionalHeader.AddressOfEntryPoint);
	if(DllMain){
		return DllMain((HMODULE)_module, ul_reason_for_call, lpReserved);
	}
	return 0;
}


// Ĩȥͷ��Ϣ
int __stdcall PELoader::HackHeader(){
	if(!_module) return 0;
	UINT_PTR img = (UINT_PTR) _module;
	PIMAGE_NT_HEADERS nth = NTHEADER(_module);
	
	// ȥ��OptionalHeader��־
	nth->OptionalHeader.Magic = 0;
	// ȥ��Dll����
	IMAGE_DATA_DIRECTORY idd = nth->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if(idd.Size){
		IMAGE_EXPORT_DIRECTORY* ied = (IMAGE_EXPORT_DIRECTORY*)(img + idd.VirtualAddress);
		*(char*)(img + ied->Name) = 0;	//���ַ����ĵ�һ�ֽڸ�Ϊ0
	}
	// ����NtHeader����Ϣֱ����0
	UINT_PTR fillsize = (UINT_PTR) &nth->OptionalHeader - img;
	ZeroMemory((void*)img, fillsize);
	return 0;
}


// ��������ĳ���
SIZE_T CaleAlignmentSize(int origin, int alignment){
	return (origin + alignment - 1) / alignment * alignment;
}

// ����Զ��PEӳ��, ���õ�Զ���ߵĳ���ʼ����
// ͨ����Զ��ʹ����ɾ���������ͷ�Զ��PE�ڴ�, ���ײ�ʾ��
// �������ݷ���image����
void* __stdcall PELoader::Inject(HANDLE hProcess, char* StartFunc, void** ThreadMain){
	if(!(_module && hProcess)) return 0;
	UINT_PTR ThreadStart = (UINT_PTR) GetProcAddr(StartFunc);
	if(!ThreadStart){
		outlog(L"PE��δ�����߳���ʼ����!\n");
		return 0;
	}

	SIZE_T size_data = CaleAlignmentSize(_extdata_size, 4096);
	
	//SIZE_T size_data = _aligned_msize()
	// ��������
	//SIZE_T size_data = 4096;
	//char params[4096];
	//GetCurrentDirectory(MAX_PATH, (wchar_t*)params);
	
	IMAGE_NT_HEADERS* nth = NTHEADER(_module);
	UINT_PTR size_image = nth->OptionalHeader.SizeOfImage;

	void* remote = VirtualAllocEx(hProcess, 0, size_image + size_data, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if(!remote){ return 0; }
	FixupImports();
	FixupRelocations(remote);
	
	// ������ʼ�̺߳�����Զ�̵�ַ
	*(UINT_PTR*)ThreadMain = (UINT_PTR)remote + (ThreadStart - (UINT_PTR)_module);
	// д��Զ��
	SIZE_T written = 0;
	WriteProcessMemory(hProcess, remote, (void*)_module, size_image, &written);
	if(written != size_image){
		return 0;
	}

	if (size_data){
		// ���ݸ���image����
		written = 0;
		WriteProcessMemory(hProcess, ((char*)remote + size_image), _extdata, size_data, &written);
		if (written != size_data){
			return 0;
		}
	}
	

	return remote;
}






int __stdcall PELoader::EnableDebugPrivileges(){
	HANDLE token = 0;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token)) {
		outlog(L"�򿪽�����Ȩ����ʧ��! ������:%d\n", GetLastError());
		return 0;
	}
	TOKEN_PRIVILEGES tkp;
	tkp.PrivilegeCount = 1;
	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tkp.Privileges[0].Luid);
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!AdjustTokenPrivileges(token, FALSE, &tkp, sizeof(tkp), NULL, NULL))
	{
		outlog(L"����������Ȩʧ��! ������:%d\n", GetLastError());
		return FALSE;
	}
	outlog(L"����Ȩ�޳ɹ�!\n");
	if (ERROR_NOT_ALL_ASSIGNED == GetLastError()) {
		outlog(L"��ʾ:�������б����õ���Ȩ���鶼��������з���\n");
		SetLastError(0);
	}
	CloseHandle(token);
	return 1;
}

// �ж��Ƿ��Թ���Ա�������
int __stdcall PELoader::IsElevatedToken(){
	OSVERSIONINFOEX ovi;
	memset(&ovi, 0, sizeof(OSVERSIONINFOEX));
	ovi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	if (GetVersionEx((LPOSVERSIONINFO)&ovi)){
		if (ovi.dwMajorVersion < 6){
			// ����Vista
			return 1;
		}
	}
	WORD version = LOWORD(GetVersion());
	version = MAKEWORD(HIBYTE(version), LOBYTE(version));
	if (version < 0x0600){ // ����Vista������ҪȨ����֤
		return 1;
	}

	// VC6�趨��
	struct /*TOKEN_ELEVATION*/ {
		DWORD TokenIsElevated;
	} te = { 0 };
	enum { TokenElevation = 20 };

	HANDLE token = 0;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		DWORD len;
		GetTokenInformation(token, (TOKEN_INFORMATION_CLASS)TokenElevation, &te, sizeof(te), &len);
	}
	return te.TokenIsElevated;
}

// ��ȡģ���ļ���Ŀ¼,Ĭ��Ϊ��ǰ��ִ���ļ�
int __stdcall PELoader::GetCurrentModuleDir(wchar_t* dir, int maxlen){
	HMODULE	module = GetModuleHandle(0);
	//ZeroMemory(dir, sizeof(wchar_t)*maxlen);
	if(!GetModuleFileName(module, dir, maxlen)){
		return 0;
	}
	wchar_t* i = wcsrchr(dir, '\\');
	if(i) *i=0;
	return 1;
}

int __stdcall PELoader::SetCurrentExecDir(){
	wchar_t buffer[MAX_PATH];
	ZeroMemory(buffer, sizeof(buffer));
	if(!GetCurrentModuleDir(buffer, MAX_PATH)){
		return 0;
	}
	return SetCurrentDirectory(buffer);
}

// ��ʵ������֮���ٹرձ�����
int __stdcall PELoader::RunasAdmin(const wchar_t* params){
	wchar_t file[MAX_PATH];
	ZeroMemory(file, sizeof(wchar_t) * MAX_PATH);
	if(!GetModuleFileName(NULL, file, MAX_PATH)){
		return 0;
	}
	
	SHELLEXECUTEINFO sei;
	ZeroMemory(&sei, sizeof(SHELLEXECUTEINFO));
	sei.cbSize = sizeof(SHELLEXECUTEINFO);
	sei.lpVerb = L"runas";
	sei.lpFile = file;
	sei.lpParameters = params;
	sei.nShow = SW_SHOWNORMAL;
	
	if(ShellExecuteEx(&sei)){
		return 1;
	}
	
	DWORD err = GetLastError();
	if(ERROR_CANCELLED == err){
		outlog(L"ȡ��ִ��\n");
	}
	else if(ERROR_FILE_NOT_FOUND == err){
		outlog(L"�ļ�δ�ҵ�\n");
	}
	return 0;
}


int __stdcall PELoader::TestInject(HWND hwnd, wchar_t* dllfile, int wait){
	if(!hwnd){
		outlog(L"���ڲ�����!\n");
		return 0;
	}
	// �򿪽���
	DWORD pid = 0;
	if(!GetWindowThreadProcessId(hwnd, &pid)){
		return 0;
	}
	HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);
	
	if(!process){
		if(ERROR_ACCESS_DENIED == GetLastError()){
			outlog(L"��Ȩ����Ŀ�����!\n");
			if(!IsElevatedToken()) return RunasAdmin(0);
			return -1;
		}
		outlog(L"�򿪽���ʧ�ܣ�\n");
		return 0;
	}

	SetCurrentExecDir();

	char extdata[4096];
	*(HWND*)(extdata) = hwnd;
	GetCurrentDirectory(4094, (wchar_t*)(extdata + 4));

	PELoader loader;
	if(!loader.CreateImage(dllfile)){
		CloseHandle(process);
		outlog(L"����dll�ļ�ʧ��!\n");
		return 0;
	}
	loader.SetExtData(extdata, 4096);

	void* ThreadMain = 0;
	void* remote = loader.Inject(process, "RemoteCRTStartup", &ThreadMain);
	if(!remote){
		outlog(L"����Զ��PEʧ��!\n");
		CloseHandle(process);
		return 0;
	}

	HANDLE hThread = CreateRemoteThread(process, 0, 0, (LPTHREAD_START_ROUTINE)ThreadMain, (void*)remote, 0, 0);
	if(!hThread){
		outlog(L"����Զ���߳�ʧ��!\n");
		CloseHandle(process);
		return 0;
	}

	outlog(L"Զ���߳�������!\n");

	if(wait){
		WaitForSingleObject(hThread, -1);
		DWORD code = 0;
		int temp = GetExitCodeThread(hThread, &code);
		outlog(L"�߳̽���,����ֵ:%d\n", code);
		CloseHandle(hThread);
		if(loader.UnInject(process, remote)){
			outlog(L"Զ���ڴ�ռ����ͷţ�\n");
		}
	}
	
	CloseHandle(process);
	return 1;
}