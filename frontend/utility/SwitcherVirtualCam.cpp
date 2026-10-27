#include "SwitcherVirtualCam.hpp"

#include <obs-module.h>
#include <util/platform.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#include <string>
#endif

bool SwitcherInstallVirtualCam(QWidget *)
{
#ifdef _WIN32
	obs_module_t *dshow = obs_get_module("win-dshow");
	char *script = dshow ? obs_find_module_file(dshow, "virtualcam-install.bat") : nullptr;
	if (!script) {
		blog(LOG_WARNING, "Switcher: virtualcam-install.bat not found");
		return false;
	}

	wchar_t *wscript = nullptr;
	os_utf8_to_wcs_ptr(script, 0, &wscript);
	bfree(script);
	if (!wscript) {
		return false;
	}

	/* Through cmd so the script's own folder is the working directory */
	std::wstring params = L"/c \"\"" + std::wstring(wscript) + L"\"\"";
	bfree(wscript);

	SHELLEXECUTEINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = L"runas";
	info.lpFile = L"cmd.exe";
	info.lpParameters = params.c_str();
	info.nShow = SW_HIDE;

	/* Fails when the user declines the administrator prompt */
	if (!ShellExecuteExW(&info) || !info.hProcess) {
		blog(LOG_WARNING, "Switcher: virtual camera install was not run (%lu)", GetLastError());
		return false;
	}

	WaitForSingleObject(info.hProcess, 60000);
	CloseHandle(info.hProcess);

	/* The same check win-dshow makes before offering the virtual camera,
	 * with the VIRTUALCAM_GUID the Windows build presets use */
	HKEY key = nullptr;
	LSTATUS status = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\{A3FCE0F5-3493-419F-958A-ABA1250EC20B}", 0,
				       KEY_READ | KEY_WOW64_32KEY, &key);
	if (status == ERROR_SUCCESS) {
		RegCloseKey(key);
	}
	blog(LOG_INFO, "Switcher: virtual camera install %s", status == ERROR_SUCCESS ? "succeeded" : "failed");
	return status == ERROR_SUCCESS;
#else
	return false;
#endif
}
