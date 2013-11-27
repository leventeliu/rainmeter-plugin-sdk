#include <cstdio>
#include <cstring>

#include <map>
using std::pair;
using std::map;
#include <list>
using std::list;


#include <Windows.h>
#include "../../API/RainmeterAPI.h"

typedef map<unsigned long, void *>	hk_map;
typedef pair<unsigned long, void *>	hk_map_pair;

static hk_map hot_keys;
static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);

class thread_hook{

private:

	static unsigned	lock;

	static DWORD	thread_id;
	static HHOOK	hook;
	static unsigned	ref_count;

public:

	static void add_hook(const DWORD &curr_tid){
		// asks for access right
		unsigned access_count;
		for (;;){
			access_count = InterlockedIncrement(&lock);
			if (access_count == 1) break;
			InterlockedDecrement(&lock);
		}
		//
		if (thread_id == 0UL){
			hook = SetWindowsHookEx(WH_GETMESSAGE, HookProc, NULL, curr_tid);
			if (hook == NULL){
				RmLog(LOG_ERROR, L"Test.dll: set message hook: failed");
			}
			else{
				thread_id = curr_tid;
				ref_count = 1U;
				RmLog(LOG_DEBUG, L"Test.dll: set message hook: succeed");
			}
		}
		else if (thread_id == curr_tid){
			++ ref_count;	
		}
		InterlockedDecrement(&lock);
	}
	static void del_hook(const DWORD &thread_id){

	}

};

unsigned	thread_hook::lock		= 0U;
DWORD		thread_hook::thread_id	= 0UL;
HHOOK		thread_hook::hook		= NULL;
unsigned	thread_hook::ref_count	= 0U;

class rm_measure{

private:

	void		   *skin;
	HWND			skin_window;
	DWORD			skin_thread_id;

	union{
		unsigned long as_ulong;
		struct{
			unsigned short modifiers;
			unsigned short vk_code;
		} as_2ushort;
	}	 hot_key;
	WCHAR			command[256];

	ATOM			atom_value;
	BOOL			key_registered;

public:

	rm_measure(void *rm):
		atom_value(0), key_registered(false)
	{
		WCHAR log_message[1024];	// for logging
		// get skin
		skin = RmGetSkin(rm);
		// get skin window handle
		skin_window = RmGetSkinWindow(rm);
		// get thread id, set hook
		skin_thread_id = GetWindowThreadProcessId(skin_window, NULL);
		thread_hook::add_hook(skin_thread_id);
		// read hot key setting
		LPCWSTR key_name = RmReadString(rm, L"Key", L"");
		hot_key.as_2ushort.vk_code = translate_key_code(key_name);
		if (hot_key.as_2ushort.vk_code == 0){
			wsprintf(log_message, L"Test.dll: Invalid key value: %s", key_name);
			RmLog(LOG_ERROR, log_message);
			return;
		}
		// read modifiers setting
		int key_alt, key_ctrl, key_shift, key_win;
		key_alt = RmReadInt(rm, L"Alt", 0);
		key_ctrl = RmReadInt(rm, L"Ctrl", 0);
		key_shift = RmReadInt(rm, L"Shift", 0);
		key_win = RmReadInt(rm, L"Win", 0);
		// make modifiers
		hot_key.as_2ushort.modifiers = 0x00;
		if (key_alt)	hot_key.as_2ushort.modifiers |= MOD_ALT;
		if (key_ctrl)	hot_key.as_2ushort.modifiers |= MOD_CONTROL;
		if (key_shift)	hot_key.as_2ushort.modifiers |= MOD_SHIFT;
		if (key_win)	hot_key.as_2ushort.modifiers |= MOD_WIN;
		wsprintf(log_message, L"Test.dll: confirm hot key: ");
		if (key_alt) wsprintf(log_message+wcslen(log_message), L"ALT+");
		if (key_ctrl) wsprintf(log_message+wcslen(log_message), L"CTRL+");
		if (key_shift) wsprintf(log_message+wcslen(log_message), L"SHIFT+");
		if (key_win) wsprintf(log_message+wcslen(log_message), L"WIN+");
		wsprintf(log_message+wcslen(log_message), L"%hx", hot_key.as_2ushort.vk_code);
		wsprintf(log_message+wcslen(log_message), L": %lx", hot_key.as_ulong);
		RmLog(LOG_DEBUG, log_message);
		// get command
		LPCWSTR cmd_temp = RmReadString(rm, L"Command", L"");
		wcscpy_s(command, cmd_temp);
		wsprintf(log_message, L"Test.dll: confirm command: %s", command);
		RmLog(LOG_DEBUG, log_message);
		// get atom
		WCHAR atom_string[256];
		LPCWSTR skin_name = RmGetSkinName(rm);
		wcscpy_s(atom_string, skin_name);
		LPCWSTR measure_name = RmGetMeasureName(rm);
		wcscat_s(atom_string, L"\\");
		wcscat_s(atom_string, measure_name);
		if (GlobalFindAtom(atom_string) != 0){
			wsprintf(log_message, L"Test.dll: found global atom: %s", atom_string);
			RmLog(LOG_ERROR, log_message);
			return;
		}
		atom_value = GlobalAddAtom(atom_string);
		wsprintf(log_message, L"Test.dll: add global atom: %s: ", atom_string);
		if (atom_value < 0xC000){
			wsprintf(log_message+wcslen(log_message), L"failed");
			RmLog(LOG_ERROR, log_message);
			return;
		}
		wsprintf(log_message+wcslen(log_message), L"succeed");
		RmLog(LOG_DEBUG, log_message);
		// register hot key
		key_registered = RegisterHotKey(skin_window, atom_value,
			hot_key.as_2ushort.modifiers, hot_key.as_2ushort.vk_code);
		if (!key_registered){
			RmLog(LOG_ERROR, L"Test.dll: register hot key: failed");
			return;
		}
		RmLog(LOG_DEBUG, L"Test.dll: register hot key: succeed");
		hot_keys[hot_key.as_ulong] = this;	// add to hotkey list
	}

	~rm_measure(){
		// delete hook
		thread_hook::del_hook(skin_thread_id);
		//
		if (key_registered){
			UnregisterHotKey(skin_window, atom_value);
			hot_keys.erase(hot_key.as_ulong);
		}
		//
		if (atom_value != 0) GlobalDeleteAtom(atom_value);
	}

	void run_command(){ RmExecute(skin, command); }

private:

	// return a Virtual-Key Code
	static unsigned short translate_key_code(LPCWSTR wstr){
		unsigned short result = 0x00;
		// get length
		size_t len = wcslen(wstr);
		if (len == 0) return result;
		// copy string
		WCHAR *tmp = new WCHAR[len+1];
		for (size_t i = 0; i <= len; ++ i){
			if (wstr[i] >= L'a' && wstr[i] <= L'z')
				tmp[i] = wstr[i]&WCHAR(0xDF);		// use capital case
			else
				tmp[i] = wstr[i];
		}
		// translate
		if (len == 1){
			// a number
			if (tmp[0] >= L'0' && tmp[0] <= L'9' ||
				tmp[0] >= L'A' && tmp[0] <= L'Z'){
					result = tmp[0];
			}
		}
		else{
			if (wcscmp(tmp, L"LEFT") ==0)
				result = 0x25;
			else if (wcscmp(tmp, L"UP") == 0)
				result = 0x26;
			else if (wcscmp(tmp, L"RIGHT") == 0)
				result = 0x27;
			else if (wcscmp(tmp, L"DOWN") == 0)
				result = 0x28;
		}

		delete[] tmp;

		return result;
	}

};

static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam){
	// from the previous hook: do nothing
	if (nCode < 0) return CallNextHookEx(NULL, nCode, wParam, lParam);
	//
	WCHAR log_message[1024];	// for logging
	MSG	*detail = (MSG *)lParam;
	if (detail->message == WM_HOTKEY && detail->wParam == PM_REMOVE){
		wsprintf(log_message, L"Test.dll: hook hot key: %lx", detail->lParam);
		RmLog(LOG_DEBUG, log_message);
		map<unsigned long, void *>::iterator it = hot_keys.find(detail->lParam);
		if (it != hot_keys.end()){
			wsprintf(log_message, L"Test.dll: hot key: %lx", detail->lParam);
			RmLog(LOG_DEBUG, log_message);
			//reinterpret_cast<rm_measure *>((*it).second)->run_command();
		}
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// call after memory object "rm" created(launch/refresh skin: read local .ini file)
PLUGIN_EXPORT void Initialize(void** data, void* rm)
{
	// create data
	*data = new rm_measure(rm);
}

// 
PLUGIN_EXPORT void Reload(void *, void *, double *){}

// 
PLUGIN_EXPORT void Finalize(void *data)
{
	delete reinterpret_cast<rm_measure *>(data);
}