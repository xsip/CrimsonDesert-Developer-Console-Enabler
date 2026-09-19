#include <External/Include.h>
#include <Math/Vector.h>

namespace Globals {
	LiquidHookEx::Process* pProc = nullptr;
	LiquidHookEx::RemoteModule* pExeMod = nullptr;
}

using namespace Globals;

struct CustomCommandCtx {
	Vector* pos;
    float jmpHeight;
};

static CustomCommandCtx* g_pCustomCommandCtx = nullptr;

LH_START(".hkReg")

void __fastcall CustomCommand()
{
	volatile CustomCommandCtx* data = g_pCustomCommandCtx;
    data->pos->y = data->pos->y + data->jmpHeight;
}

void CustomCommandEnd() {};

LH_END()

bool bRegistered = false;
int main() {
	SetConsoleTitle("Crimson Desert Developer Console Enabler");

	LiquidHookEx::INIT("CrimsonDesert.exe");
	pProc = LiquidHookEx::proc;
	pExeMod = pProc->GetRemoteModule("CrimsonDesert.exe");
	
	printf("CrimsonDesert.exe: 0x%p\n", pExeMod->GetAddr());
	printf("WinAppLauncher: 0x%p\n", CrimsonDesert::WinAppLauncher::GetInstance());

	if (!CrimsonDesert::WinAppLauncher::ApplyHooks()) {
		printf("Couldn't hook \"DevModeDisabler\"-Function!\n");
		return 1;
	}

	CrimsonDesert::WinAppLauncher::AllocConsole();
	auto pCmdReg = CrimsonDesert::CommandRegistry::GetInstance(true, 100);

	printf("CommandRegistry: 0x%p\n", pCmdReg);
	while (true) {
		if (GetAsyncKeyState(4) & 1) {
       
            if (!bRegistered) {
                CustomCommandCtx ctx{};
                ctx.pos = reinterpret_cast<Vector*>(CrimsonDesert::CPosition::GetInstance()->GetPositionAddr());
				printf("Pos Addr: 0x%p\n", ctx.pos);
                ctx.jmpHeight = 10.0f;
                auto customCmdData = pCmdReg->RegisterCommand<CustomCommandCtx>("/jump", CustomCommand, CustomCommandEnd, &g_pCustomCommandCtx, ctx);
                if (customCmdData.bAllocated && customCmdData.bRegisted) {
                    bRegistered = true;
                    printf("Command /jump registred!\n");
                }
                continue;
            }
            CrimsonDesert::WinAppLauncher::GetInstance()->ShowConsole();
		}
		Sleep(1);
	}
	
}