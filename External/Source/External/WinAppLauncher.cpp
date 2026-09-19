#include <External/WinAppLauncher.h>
#include <GlobalData/Include.h>
#include <LiquidHookEx/Include.h>
#include <External/Patterns.h>

using namespace Globals;

namespace CrimsonDesert {
	LH_START(".togC")

		DWORD WINAPI SetConsoleStateThread(LPVOID lpParam) {
		WinAppLauncher::ConsoleStateCtx* ctx = reinterpret_cast<WinAppLauncher::ConsoleStateCtx*>(lpParam);


		if (ctx->bShow) {
			using ShowConsoleFn = void(*)(WinAppLauncher*);
			ShowConsoleFn Exec = reinterpret_cast<ShowConsoleFn>(ctx->ShowConsoleFn);
			Exec(ctx->pThis);
		}
		else {
			using HideConsoleFn = void(*)(WinAppLauncher*);
			HideConsoleFn  Exec = reinterpret_cast<HideConsoleFn>(ctx->HideConsoleFn);
			Exec(ctx->pThis);
		}


		return 1;
	}

	DWORD WINAPI SetConsoleStateThreadEnd() { return 0; }
	LH_END()
		LH_START(".all")

		DWORD WINAPI AllocConsoleThread(LPVOID lpParam) {
		WinAppLauncher::AllocConsoleCtx* ctx = reinterpret_cast<WinAppLauncher::AllocConsoleCtx*>(lpParam);

		if (!ctx->bDebugEnabled) {
			using AllocConsoleDef = BOOL(WINAPI*)();
			AllocConsoleDef _AllocConsole = reinterpret_cast<AllocConsoleDef>(ctx->AllocConsole);
			if (_AllocConsole()) {
				using FreopenDef = FILE * (__cdecl*)(const char*, const char*, FILE*);
				FreopenDef Freopen = reinterpret_cast<FreopenDef>(ctx->Freopen);
				// freopen("CONOUT$", "w", stdout);
				using ACRTIOBFuncDef = FILE * (__cdecl*)(unsigned int);
				ACRTIOBFuncDef GetStdOut = reinterpret_cast<ACRTIOBFuncDef>(ctx->GetStdOut);
				Freopen(ctx->szConOut, ctx->szW, GetStdOut(ctx->m_StdOutIdx));
				ctx->bDebugEnabled = true;
			}
		}

		return 1;
	}

	DWORD WINAPI AllocConsoleThreadEnd() { return 0; }
	LH_END()



		static void* g_pOriginalDisabler = nullptr;
	static WinAppLauncher::DisablerHookData* g_pDisablerHookData = nullptr;

	LH_START(".hkDisabler")

		__int64 __fastcall WinAppLauncher::hkDisabler(__int64 a1, __int64 a2, __int64 a3, __int64 a4)
	{

		volatile WinAppLauncher::DisablerHookData* data = g_pDisablerHookData;

		typedef __int64(__fastcall* DisablerFn)(__int64 a1, __int64 a2, __int64 a3, __int64 a4);
		volatile DisablerFn original = (DisablerFn)g_pOriginalDisabler;
		return 0x0;

	}

	void WinAppLauncher::hkDisablerEnd() {}

	LH_END()


		bool WinAppLauncher::ApplyHooks() {
		

		DisablerHookData initData{};

		return m_Hook.Hook<DisablerHookData>(
			DISABLE_FN_PATTERN,
			"CrimsonDesert.exe",
			initData,
			reinterpret_cast<void*>(hkDisabler),
			reinterpret_cast<void*>(hkDisablerEnd),
			{
				LiquidHookEx::Detour::RipSlot::Data(&g_pDisablerHookData),
				LiquidHookEx::Detour::RipSlot::Orig(&g_pOriginalDisabler),
			},
			17
			);
	}



	WinAppLauncher* WinAppLauncher::GetInstance() {
		return pProc->ReadDirect<WinAppLauncher*>(pExeMod->ResolveRIP(pExeMod->ScanMemory(WIN_APP_LAUNCHER_MOV_RCX_INSTR)));
	}

	void WinAppLauncher::AllocConsole() {
		if (!pAllocConsoleCtx) {
			auto pBaseAddr = pExeMod->GetAddr();
			AllocConsoleCtx ctx;

			auto kernel32 = GetModuleHandle("KERNEL32.dll");
			if (!kernel32) {
				printf("Error retrieving KERNEL32.dll for \"AllocConsole\"-Thread.\n");
				return;
			}
			ctx.AllocConsole = reinterpret_cast<AllocConsoleDef>(GetProcAddress(kernel32, "AllocConsole"));

			auto ucrtbase = GetModuleHandle("ucrtbase.dll");
			if (!ucrtbase) {
				printf("Error retrieving ucrtbase.dll for \"AllocConsole\"-Thread.\n");
				return;
			}
			ctx.Freopen = reinterpret_cast<FreopenDef>(GetProcAddress(ucrtbase, "freopen"));
			ctx.GetStdOut = reinterpret_cast<ACRTIOBFuncDef>(__acrt_iob_func);
			ctx.m_StdOutIdx = 1;

			pRemoteConOutStr = pProc->AllocateAndWriteString("CONOUT$");
			if (!pRemoteConOutStr) {
				printf("Error allocating \"CONOUT$\" string for \"AllocConsole\"-Thread.\n");
				return;
			}

			ctx.szConOut = reinterpret_cast<const char*>(pRemoteConOutStr);


			pRemoteWStr = pProc->AllocateAndWriteString("w");
			if (!pRemoteWStr) {
				printf("Error allocating \"w\" string for \"AllocConsole\"-Thread.\n");
				pProc->FreeRemote(pRemoteConOutStr);
				pRemoteConOutStr = nullptr;
				return;
			}

			ctx.szW = reinterpret_cast<const char*>(pRemoteWStr);


			ctx.bDebugEnabled = false;

			pAllocConsoleCtx = reinterpret_cast<AllocConsoleCtx*>(pProc->Alloc(sizeof(ctx)));

			if (!pAllocConsoleCtx) {
				printf("Error Allocating CTX for \"AllocConsole\"-Thread.\n");
				return;
			}

			if (!pProc->Write<AllocConsoleCtx>(reinterpret_cast<uintptr_t>(pAllocConsoleCtx), ctx)) {
				printf("Error writing CTX for \"AllocConsole\"-Thread.\n");
				pProc->FreeRemote(pAllocConsoleCtx);
				pAllocConsoleCtx = nullptr;
				return;
			}
		}

		if (!pAllocConsoleShellcode) {
			pAllocConsoleShellcode = pProc->AllocAndWriteShellcode(AllocConsoleThread, AllocConsoleThreadEnd);
			if (!pAllocConsoleShellcode) {
				printf("Error writing Shellcode for \"AllocConsole\"-Thread.\n");
				return;
			}
		}

		HANDLE hThread = pProc->CreateRemoteThreadEx(
			reinterpret_cast<LPTHREAD_START_ROUTINE>(pAllocConsoleShellcode),
			pAllocConsoleCtx
		);

		if (!hThread) {
			printf("Error creating \"AllocConsole\"-Thread.\n");
			pProc->FreeRemote(pAllocConsoleShellcode);
			pAllocConsoleShellcode = nullptr;
			pProc->FreeRemote(pAllocConsoleCtx);
			pAllocConsoleCtx = nullptr;
			return;
		}

		DWORD waitResult = WaitForSingleObject(hThread, 10000);

		if (waitResult != WAIT_OBJECT_0) {
			printf("\"AllocConsole\"-Thread timed out. ( %d )\n", waitResult);
			CloseHandle(hThread);
			pProc->FreeRemote(pAllocConsoleShellcode);
			pAllocConsoleShellcode = nullptr;
			pProc->FreeRemote(pAllocConsoleCtx);
			pAllocConsoleCtx = nullptr;

			return;
		}

		DWORD exitCode = 0;
		GetExitCodeThread(hThread, &exitCode);
		CloseHandle(hThread);
	}


	void WinAppLauncher::SetConsoleVisibility(bool bIsVisible) {
		if (!pRemoteConsoleCtx) {
			auto pBaseAddr = pExeMod->GetAddr();
			ConsoleStateCtx ctx;
			ctx.ShowConsoleFn = reinterpret_cast<PearlAbyssEngine_ShowDebugConsole>(pExeMod->ScanMemory(SHOW_CONSOLE_FN_PATTERN));
			ctx.HideConsoleFn = reinterpret_cast<PearlAbyssEngine_HideDebugConsole>(pExeMod->ScanMemory(HIDE_CONSOLE_FN_PATTERN));


			ctx.pThis = this;
			ctx.bShow = bIsVisible;

			pRemoteConsoleCtx = reinterpret_cast<ConsoleStateCtx*>(pProc->Alloc(sizeof(ctx)));

			if (!pRemoteConsoleCtx) {
				printf("Error Allocating CTX for \"SetConsoleState\"-Thread.\n");
				return;
			}

			if (!pProc->Write<ConsoleStateCtx>(reinterpret_cast<uintptr_t>(pRemoteConsoleCtx), ctx)) {
				printf("Error writing CTX for \"SetConsoleState\"-Thread.\n");
				pProc->FreeRemote(pRemoteConsoleCtx);
				pRemoteConsoleCtx = nullptr;
				return;
			}
		}

		pProc->Write<size_t>(reinterpret_cast<uintptr_t>(pRemoteConsoleCtx) + offsetof(ConsoleStateCtx, bShow), bIsVisible);


		if (!pHandleConsoleVisibilityShellcode) {
			pHandleConsoleVisibilityShellcode = pProc->AllocAndWriteShellcode(SetConsoleStateThread, SetConsoleStateThreadEnd);
			if (!pHandleConsoleVisibilityShellcode) {
				printf("Error Allocating Shellcode for \"SetConsoleState\"-Thread.\n");
				return;
			}
		}

		HANDLE hThread = pProc->CreateRemoteThreadEx(
			reinterpret_cast<LPTHREAD_START_ROUTINE>(pHandleConsoleVisibilityShellcode),
			pRemoteConsoleCtx
		);

		if (!hThread) {
			printf("Error creating \"SetConsoleState\"-Thread.\n");
			pProc->FreeRemote(pHandleConsoleVisibilityShellcode);
			pHandleConsoleVisibilityShellcode = nullptr;
			pProc->FreeRemote(pRemoteConsoleCtx);
			pRemoteConsoleCtx = nullptr;
			return;
		}

		DWORD waitResult = WaitForSingleObject(hThread, 10000);

		if (waitResult != WAIT_OBJECT_0) {
			printf("\"SetConsoleState\"-Thread timed out. ( %d )\n", waitResult);
			CloseHandle(hThread);
			pProc->FreeRemote(pHandleConsoleVisibilityShellcode);
			pHandleConsoleVisibilityShellcode = nullptr;
			pProc->FreeRemote(pRemoteConsoleCtx);
			pRemoteConsoleCtx = nullptr;

			return;
		}

		DWORD exitCode = 0;
		GetExitCodeThread(hThread, &exitCode);
		CloseHandle(hThread);
	}




	void WinAppLauncher::ShowConsole() {
		SetConsoleVisibility(true);
	}
	void WinAppLauncher::HideConsole() {
		SetConsoleVisibility(false);
	}


	void WinAppLauncher::EnableDevMode() {

	}
}