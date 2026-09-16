#include <External/CommandRegistry.h>
#include <GlobalData/Include.h>
#include <LiquidHookEx/Include.h>

using namespace Globals;

namespace CrimsonDesert {

	LH_START(".regCmd")

	DWORD WINAPI CommandRegistry::RegisterCmdThread(LPVOID lpParam) {
		CommandRegistry::RegisterCustomCommandCtx* ctx = reinterpret_cast<CommandRegistry::RegisterCustomCommandCtx*>(lpParam);

		typedef int(__cdecl* RegisterCmdDef)(void* registry,
			const char* command,
			const char* handlerName,
			CommandRegistry::RegisterCustomCommandHandlerDescriptorCtx* descriptor);
		RegisterCmdDef RegCmd = reinterpret_cast<RegisterCmdDef>(ctx->fnToExecute);


		RegCmd(ctx->pRegistry, ctx->szCmd, ctx->szConsoleCommandHandler, ctx->pDescHandler);

		return 1;
	}
	DWORD WINAPI CommandRegistry::RegisterCmdThreadEnd() { return 0; }
	LH_END()

	CommandRegistry* CommandRegistry::GetInstance(bool bWaitFor, int iWaitForTimeout) {
		CommandRegistry* pReg = pProc->ReadDirect<CommandRegistry*>(pProc->ReadDirect<uintptr_t>(pProc->ReadDirect<uintptr_t>(pExeMod->GetAddr() + 0x6B53230)) + 0x28);
		if (!pReg && bWaitFor) {
			while (!(pReg = GetInstance(false))) {
				Sleep(iWaitForTimeout);
			}
		}
		return pReg;
	}


	bool CommandRegistry::PatchCustomCommandCtx(
		std::string m_szName,
		std::vector<uint8_t>& localCode,
		void* m_pShellcodeRemote,
		size_t               shellcodeSize,
		void* fnStart,
		void* pLocalData,
		void* m_pDataRemote)
	{
		int patched = 0;
		void* remoteSlot = nullptr;

		for (size_t i = 0; i + 7 <= shellcodeSize; ++i) {
			// mov rax, [rip + disp32]
			if (localCode[i] != 0x48 ||
				localCode[i + 1] != 0x8B ||
				localCode[i + 2] != 0x05)
				continue;

			int32_t localDisp;
			std::memcpy(&localDisp, &localCode[i + 3], sizeof(localDisp));

			uintptr_t localRip =
				reinterpret_cast<uintptr_t>(fnStart) + i + 7;

			uintptr_t localTarget =
				localRip + localDisp;

			// Only patch the declared local HookData variable.
			if (localTarget != reinterpret_cast<uintptr_t>(pLocalData))
				continue;

			// Allocate the remote indirection slot once.
			if (!remoteSlot) {
				remoteSlot = proc->Alloc(8);
				if (!remoteSlot) {
					printf("\t\t[PrepareCommand::PatchCtx][%s] failed to allocate remote context data slot\n",
						m_szName.c_str());
					return false;
				}

				if (!proc->Write<uint64_t>(
					reinterpret_cast<uintptr_t>(remoteSlot),
					reinterpret_cast<uint64_t>(m_pDataRemote))) {
					printf("\t\t[PrepareCommand::PatchCtx][%s] failed to write remote context data slot\n",
						m_szName.c_str());
					return false;
				}

				printf("\t\t[PrepareCommand::PatchCtx][%s] context data slot 0x%p → 0x%llX\n",
					m_szName.c_str(),
					remoteSlot,
					reinterpret_cast<uint64_t>(m_pDataRemote));
			}

			uintptr_t remoteInstrAddr =
				reinterpret_cast<uintptr_t>(m_pShellcodeRemote) + i;

			intptr_t offset =
				reinterpret_cast<uintptr_t>(remoteSlot) -
				(remoteInstrAddr + 7);

			// RIP-relative displacement is signed 32-bit.
			if (offset < INT32_MIN || offset > INT32_MAX) {
				printf("\t\t[PrepareCommand::PatchCtx][%s] Context data slot is out of RIP-relative range at +0x%zX\n",
					m_szName.c_str(), i);
				return false;
			}

			int32_t newOffset = static_cast<int32_t>(offset);

			if (!proc->Write<int32_t>(
				remoteInstrAddr + 3, newOffset)) {
				printf("\t\t[PrepareCommand::PatchCtx][%s] failed to patch Context data disp32 at +0x%zX\n",
					m_szName.c_str(), i);
				return false;
			}

			++patched;

			printf("\t\t[PrepareCommand::PatchCtx][%s] Context data RIP +0x%zX → slot 0x%p\n",
				m_szName.c_str(), i, remoteSlot);
		}

		printf("\t\t[PrepareCommand::PatchCtx][%s] patched %d Context data RIP load(s)\n",
			m_szName.c_str(), patched);

		return true;
	}
	void CommandRegistry::RegisterCommandInternal(CommandRegistry::CustomCommandRemoteData<void> *cmdData) {

		cmdData->bRegisted = false;
		if (!reinterpret_cast<void*>(this)) {
			printf("\t[RegisterCommand][%s] Registry Seems to be invalid: 0x%p\n", cmdData->szCmdName.c_str(), !reinterpret_cast<void*>(this));
			return;
		}


		void* pCmdName = pProc->AllocateAndWriteString(cmdData->szCmdName);
		if (!pCmdName) {
			printf("\t[RegisterCommand][%s] Couldnt write command identifier for registration\n", cmdData->szCmdName.c_str());
			return;
		}


		void* pEngineConsoleCommandHandler = pProc->AllocateAndWriteString("EngineConsoleCommandHandler");
		if (!pEngineConsoleCommandHandler) {
			printf("\t[RegisterCommand][%s] Couldnt write \"EngineConsoleCommandHandler\" identifier for registration\n", cmdData->szCmdName.c_str());
			return;
		}

		RegisterCustomCommandCtx customCmdCtx{};
		// // [[[CrimsonDesert.exe+6B53230]] + 0x28]
		customCmdCtx.pRegistry = reinterpret_cast<void*>(this);
		customCmdCtx.szCmd = reinterpret_cast<const char*>(pCmdName);
		customCmdCtx.szConsoleCommandHandler = reinterpret_cast<const char*>(pEngineConsoleCommandHandler);

#define REGISTER_COMMAND_FN_PATTERN "48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 4D 8B E1 49 8B C0"
		
		customCmdCtx.fnToExecute = reinterpret_cast<RegisterCmdDef>(pExeMod->ScanMemory(REGISTER_COMMAND_FN_PATTERN));

		auto pDescCtxRemote = pProc->Alloc(sizeof(RegisterCustomCommandHandlerDescriptorCtx));
		RegisterCustomCommandHandlerDescriptorCtx descriptorCtx{};
		descriptorCtx.field0 = 0x0;
		descriptorCtx.callback = reinterpret_cast<void(*)()>(cmdData->pRemoteFnAddr); // pExeMod->GetAddr() + 0x389EF80);
		descriptorCtx.field10 = 0;
		descriptorCtx.field18 = 0;

		if (!pDescCtxRemote) {
			printf("\t[RegisterCommand][%s] Couldn't allocate descriptor context for registration\n", cmdData->szCmdName.c_str());

			return;
		}

		if (!pProc->Write<RegisterCustomCommandHandlerDescriptorCtx>(reinterpret_cast<uintptr_t>(pDescCtxRemote), descriptorCtx)) {
			printf("\t[RegisterCommand][%s] Couldn't write descriptor context for registration\n", cmdData->szCmdName.c_str());
			pProc->FreeRemote(pDescCtxRemote);
			pDescCtxRemote = nullptr;
			return;
		}



		customCmdCtx.pDescHandler = reinterpret_cast<RegisterCustomCommandHandlerDescriptorCtx*>(pDescCtxRemote);


		auto pCustomCmdCtx = reinterpret_cast<RegisterCustomCommandCtx*>(pProc->Alloc(sizeof(customCmdCtx)));

		if (!pCustomCmdCtx) {
			printf("\t[RegisterCommand][%s] Couldn't allocate main context for registration\n", cmdData->szCmdName.c_str());
			return;
		}

		if (!pProc->Write<RegisterCustomCommandCtx>(reinterpret_cast<uintptr_t>(pCustomCmdCtx), customCmdCtx)) {
			printf("\t[RegisterCommand][%s] Couldn't write main context for registration\n", cmdData->szCmdName.c_str());

			pProc->FreeRemote(pCustomCmdCtx);
			pCustomCmdCtx = nullptr;
			pProc->FreeRemote(pDescCtxRemote);
			pDescCtxRemote = nullptr;
			return;
		}


		auto pCustomCmdRegShellcode = pProc->AllocAndWriteShellcode(RegisterCmdThread, RegisterCmdThreadEnd);
		if (!pCustomCmdRegShellcode) {
			printf("\t[RegisterCommand][%s] Couldn't allocate and write shellcode for registration\n", cmdData->szCmdName.c_str());
			return;
		}

		HANDLE hThread = pProc->CreateRemoteThreadEx(
			reinterpret_cast<LPTHREAD_START_ROUTINE>(pCustomCmdRegShellcode),
			pCustomCmdCtx
		);

		if (!hThread) {
			printf("\t[RegisterCommand][%s] Failed to create remote registration thread\n", cmdData->szCmdName.c_str());
			pProc->FreeRemote(pCustomCmdRegShellcode);
			pCustomCmdRegShellcode = nullptr;
			pProc->FreeRemote(pCustomCmdCtx);
			pCustomCmdCtx = nullptr;
			pProc->FreeRemote(pDescCtxRemote);
			pDescCtxRemote = nullptr;
			return;
		}

		DWORD waitResult = WaitForSingleObject(hThread, 10000);

		if (waitResult != WAIT_OBJECT_0) {
			printf("\t[RegisterCommand][%s] Regsitration thread timed out\n", cmdData->szCmdName.c_str());
			CloseHandle(hThread);
			pProc->FreeRemote(pCustomCmdRegShellcode);
			pCustomCmdRegShellcode = nullptr;
			pProc->FreeRemote(pCustomCmdCtx);
			pCustomCmdCtx = nullptr;
			pProc->FreeRemote(pDescCtxRemote);
			pDescCtxRemote = nullptr;

			return;
		}

		DWORD exitCode = 0;
		GetExitCodeThread(hThread, &exitCode);
		CloseHandle(hThread);
		cmdData->bRegisted = true;
	}
}