#pragma once
#include "common.h"
#include "ui.h"

namespace fsd {

    struct OverlayInfo {
        std::atomic<int>  fps{ 0 };
        std::atomic<int>  backend{ 0 };    // 0=未知 1=D3D12 2=D3D11/不支持
        std::atomic<bool> hooked{ false };
        std::atomic<int>  bbW{ 0 }, bbH{ 0 };
        std::atomic<int>  frame{ 0 };
        std::atomic<bool> inited{ false };
        std::atomic<int>  lastError{ 0 };
    };
    extern OverlayInfo g_ovl;

    struct TrainerDeps {
        Scanner*     sc = nullptr;
        Freezer*     fz = nullptr;
        PatchEngine* pe = nullptr;
        int          uiScale = 100;
        bool         menuOpen = false;
    };
    extern TrainerDeps g_deps;
    extern std::atomic<bool> g_unloading;

    class Overlay {
    public:
        bool Install();
        void Shutdown();
        void UninstallHooks();      // 解除全部挂钩 + 恢复窗口过程（用于“END 键杀死修改器”）
        void SetUi(Ui* u) { ui = u; }
        Ui*  GetUi() { return ui; }
    private:
        Ui* ui = nullptr;
    };
    extern Overlay g_overlay;
}
