#include <External/CPosition.h>
#include <GlobalData/Include.h>
#include <External/Patterns.h>
using namespace Globals;

namespace CrimsonDesert {
	CPosition* CPosition::GetInstance() {
		if (!pInstAddr) {
			auto pPositionPatternResult = pExeMod->ScanMemory(POSITION_PTR_INSTR);
			if (!pPositionPatternResult) {
				printf("Error finding  \"CPosition\"-Pattern\n");
				return nullptr;
			}

			pInstAddr = pExeMod->ResolveRIP(
				pPositionPatternResult,
				4,
				8
			);

			if (!pInstAddr) {
				printf("Error finding  \"CPosition\"-Addr\n");
				return nullptr;
			}
		}
		return pProc->ReadDirect<CPosition*>(pInstAddr + 0x8);
	}
}