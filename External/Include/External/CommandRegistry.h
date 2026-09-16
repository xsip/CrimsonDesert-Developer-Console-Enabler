#pragma once
#include <cstddef>
#include <LiquidHookEx/Include.h>
#include <GlobalData/Include.h>
using namespace Globals;

namespace CrimsonDesert {
	class CommandRegistry {
	private:
		static DWORD WINAPI RegisterCmdThread(LPVOID lpParam);
		static DWORD WINAPI RegisterCmdThreadEnd();

		struct RegisterCustomCommandHandlerDescriptorCtx {
			void* field0;          // 0x00
			void (*callback)();    // 0x08
			uint64_t field10;      // 0x10, unknown
			uint8_t field18;       // 0x18, written as 0
			char pad[0x6];
			// padding              0x19–0x1F
		};
		static_assert(sizeof(RegisterCustomCommandHandlerDescriptorCtx) == 0x20);
		typedef int(__cdecl* RegisterCmdDef)(void* registry,
			const char* command,
			const char* handlerName,
			RegisterCustomCommandHandlerDescriptorCtx* descriptor);

		struct RegisterCustomCommandCtx {
			RegisterCmdDef fnToExecute;
			void* pRegistry; // [[[CrimsonDesert.exe+6B53230]] + 0x28]
			const char* szCmd;
			const char* szConsoleCommandHandler;
			RegisterCustomCommandHandlerDescriptorCtx* pDescHandler;
		};


		static bool PatchCustomCommandCtx(
			std::string m_szName,
			std::vector<uint8_t>& localCode,
			void* m_pShellcodeRemote,
			size_t               shellcodeSize,
			void* fnStart,
			void* pLocalData,
			void* m_pDataRemote);

	public:
		template <typename T>
		struct CustomCommandRemoteData {
			std::string szCmdName;
			void* pRemoteFnAddr;
			T* pRemoteCtxAddr;
			bool bRegisted;
			bool bAllocated;
		};


	private:

		
		template <typename T>
		inline CustomCommandRemoteData<T> PrepareCommand(std::string szCmdName, void* fnStart, void* fnEnd, void* g_pCustomCommandCtx,T ctx) {
			CustomCommandRemoteData<T> remoteData{};
			remoteData.szCmdName = szCmdName;
			remoteData.bRegisted = false;
			remoteData.bAllocated = false;
			remoteData.pRemoteCtxAddr = 0x0;
			remoteData.pRemoteFnAddr = 0x0;
			printf("\t[PrepareCommand][%s] Preparing\n", szCmdName.c_str());
			auto pCtxAllocation = pProc->Alloc(sizeof(ctx));

			if (!pCtxAllocation) {
				printf("\t[PrepareCommand][%s] Error allocating custom command ctx for!\n", szCmdName.c_str());
				remoteData.bAllocated = false;
				return remoteData;
			}

			if (!pProc->Write<T>(reinterpret_cast<uintptr_t>(pCtxAllocation), ctx)) {
				printf("\t[PrepareCommand][%s] Error writing custom command context!\n", szCmdName.c_str());
				remoteData.bAllocated = false;
				return remoteData;
			}

			printf("\t[PrepareCommand][\"%s\"] Context Allocation: 0x%p\n\n", szCmdName.c_str(), pCtxAllocation);
			remoteData.pRemoteCtxAddr = reinterpret_cast<T*>(pCtxAllocation);

			size_t shellcodeSize =
				reinterpret_cast<uintptr_t>(fnEnd) -
				reinterpret_cast<uintptr_t>(fnStart);

			void* m_pShellcodeRemote = pProc->Alloc(
				shellcodeSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (!m_pShellcodeRemote) {
				printf("\t[PrepareCommand][\"%s\"] Failed to allocate shellcode!\n", szCmdName.c_str());
				remoteData.bAllocated = false;
				return remoteData;
			}

			std::vector<uint8_t> localCode(shellcodeSize);
			memcpy(localCode.data(), fnStart, shellcodeSize);
			if (!pProc->WriteArray(
				reinterpret_cast<uintptr_t>(m_pShellcodeRemote), localCode)) {
				printf("\t[PrepareCommand][\"%s\"] Failed to write shellcode!\n", szCmdName.c_str());
				remoteData.bAllocated = false;
				return remoteData;
			}
			printf("\t[PrepareCommand][\"%s\"] Shellcode Allocation: 0x%p\n\n", szCmdName.c_str(), m_pShellcodeRemote);

			remoteData.pRemoteFnAddr = m_pShellcodeRemote;

			if (!PatchCustomCommandCtx(szCmdName, localCode, m_pShellcodeRemote, shellcodeSize, fnStart, g_pCustomCommandCtx, pCtxAllocation)) {
				printf("\t[PrepareCommand][\"%s\"] Failed to patch remote shellcode!\n", szCmdName.c_str());
				remoteData.bAllocated = false;
				return remoteData;
			}
			remoteData.bAllocated = true;
			
			return remoteData;
		}

		
		void RegisterCommandInternal(CustomCommandRemoteData<void>* cmdData);


	public:
		static CommandRegistry* GetInstance(bool bWaitFor = false, int iWaitForTimeout = 100);


		template <typename T>
		CustomCommandRemoteData<T>  RegisterCommand(std::string szCmdName, void* fnStart, void* fnEnd, void* g_pCustomCommandCtx, T ctx) {
			printf("[RegisterCommand][\"%s\"] Starting to register command!\n", szCmdName.c_str());

			CustomCommandRemoteData<T> remoteData = PrepareCommand<T>(szCmdName, fnStart, fnEnd, g_pCustomCommandCtx, ctx);
			if(!remoteData.bAllocated) {
				printf("[RegisterCommand][\"%s\"] Remote command wasn't allocated.!\n", szCmdName.c_str());
				return remoteData;
			}
			RegisterCommandInternal(reinterpret_cast<CustomCommandRemoteData<void>*>(&remoteData));
			if (!remoteData.bRegisted) {
				printf("[RegisterCommand][\"%s\"] Remote command wasn'tregistered!\n", szCmdName.c_str());
				return remoteData;
			}
			printf("[RegisterCommand][\"%s\"] Registration finished!\n", szCmdName.c_str());

			return remoteData;

		}
	};
}