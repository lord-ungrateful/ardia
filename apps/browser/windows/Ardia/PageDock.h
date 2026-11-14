#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include <windows.h>
#include <commctrl.h>

#include "LaunchLog.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")

namespace Ardia::PageDock {

constexpr UINT_PTR DockSubclassId = 0xA2D0C;
constexpr int RefusalsLogged = 5;
constexpr UINT MoveOwnerMessage = WM_APP + 0x2D0;
constexpr UINT HandStateToOwnerMessage = WM_APP + 0x2D1;

struct DockedPage {
  POINT offset{};
  bool placed{false};
  bool ownerMovePending{false};
  bool restoringItself{false};
  bool statePending{false};
  RECT ownerTarget{};
};

inline std::map<HWND, DockedPage> &Docked() noexcept {
  static std::map<HWND, DockedPage> docked;
  return docked;
}

inline std::function<bool(HWND)> &IsAppWindow() noexcept {
  static std::function<bool(HWND)> isAppWindow;
  return isAppWindow;
}

inline int &EngineCallDepth() noexcept {
  static int depth = 0;
  return depth;
}

inline int &Refusals() noexcept {
  static int refusals = 0;
  return refusals;
}

struct EngineCallScope {
  EngineCallScope() noexcept { ++EngineCallDepth(); }
  ~EngineCallScope() { --EngineCallDepth(); }
  EngineCallScope(EngineCallScope const &) = delete;
  EngineCallScope &operator=(EngineCallScope const &) = delete;
};

inline bool IsPageClass(HWND hwnd) noexcept {
  wchar_t name[16] = {};
  return GetClassNameW(hwnd, name, 16) > 0 && std::wstring_view{name}.starts_with(L"Chrome_");
}

inline bool OwnerOrigin(HWND page, POINT &origin) noexcept {
  HWND owner = GetWindow(page, GW_OWNER);
  if (!owner) return false;
  origin = {0, 0};
  return ClientToScreen(owner, &origin) != FALSE;
}

inline void Remember(HWND page) noexcept {
  auto found = Docked().find(page);
  if (found == Docked().end()) return;
  RECT rect{};
  POINT origin{};
  if (!GetWindowRect(page, &rect) || !OwnerOrigin(page, origin)) return;
  found->second.offset = {rect.left - origin.x, rect.top - origin.y};
  found->second.placed = true;
}

inline void LogRefusal(WINDOWPOS const &pos, RECT const &rect, POINT origin, POINT offset) noexcept {
  if (++Refusals() > RefusalsLogged) return;
  DWORD sent = InSendMessageEx(nullptr);
  LaunchLog::Write(L"window", std::wstring(L"page window kept docked: refused ")
                                  .append(std::to_wstring(pos.x - origin.x)).append(L",").append(std::to_wstring(pos.y - origin.y))
                                  .append(L" ").append(std::to_wstring(pos.cx)).append(L"x").append(std::to_wstring(pos.cy))
                                  .append(L" (docked at ").append(std::to_wstring(offset.x)).append(L",").append(std::to_wstring(offset.y))
                                  .append(L" ").append(std::to_wstring(rect.right - rect.left)).append(L"x").append(std::to_wstring(rect.bottom - rect.top))
                                  .append(L", flags ").append(std::to_wstring(pos.flags)).append(L", sent ").append(std::to_wstring(sent)).append(L")"));
}

inline int &PassedToOwner() noexcept {
  static int passed = 0;
  return passed;
}

inline void PassToOwner(HWND page, DockedPage &docked, WINDOWPOS const &pos, RECT const &rect, POINT origin, bool moves, bool resizes) noexcept {
  HWND owner = GetWindow(page, GW_OWNER);
  RECT ownerRect{};
  if (!owner || IsZoomed(owner) || IsIconic(owner) || !GetWindowRect(owner, &ownerRect)) return;
  RECT target = ownerRect;
  if (moves && !resizes) {
    LONG dx = pos.x - origin.x - docked.offset.x;
    LONG dy = pos.y - origin.y - docked.offset.y;
    OffsetRect(&target, dx, dy);
  } else {
    LONG left = moves ? pos.x : rect.left;
    LONG top = moves ? pos.y : rect.top;
    LONG right = left + (resizes ? pos.cx : rect.right - rect.left);
    LONG bottom = top + (resizes ? pos.cy : rect.bottom - rect.top);
    target.left += left - rect.left;
    target.top += top - rect.top;
    target.right += right - rect.right;
    target.bottom += bottom - rect.bottom;
    if (target.right - target.left < GetSystemMetrics(SM_CXMINTRACK) || target.bottom - target.top < GetSystemMetrics(SM_CYMINTRACK)) return;
  }
  if (EqualRect(&target, &ownerRect)) return;
  docked.ownerTarget = target;
  if (docked.ownerMovePending) return;
  docked.ownerMovePending = PostMessageW(page, MoveOwnerMessage, 0, 0) != FALSE;
}

inline void MoveOwner(HWND page) noexcept {
  auto found = Docked().find(page);
  if (found == Docked().end() || !found->second.ownerMovePending) return;
  found->second.ownerMovePending = false;
  HWND owner = GetWindow(page, GW_OWNER);
  if (!owner || IsZoomed(owner) || IsIconic(owner)) return;
  RECT const &target = found->second.ownerTarget;
  if (++PassedToOwner() == 1) LaunchLog::Write(L"window", L"page window passed an outside move or resize on to its app window");
  SetWindowPos(owner, nullptr, target.left, target.top, target.right - target.left, target.bottom - target.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

inline void HoldInPlace(HWND page, WINDOWPOS &pos) noexcept {
  if (EngineCallDepth() > 0 || !IsWindowVisible(page)) return;
  auto found = Docked().find(page);
  if (found == Docked().end() || !found->second.placed) return;
  POINT origin{};
  RECT rect{};
  if (!OwnerOrigin(page, origin) || !GetWindowRect(page, &rect)) return;
  bool moves = !(pos.flags & SWP_NOMOVE) && (pos.x - origin.x != found->second.offset.x || pos.y - origin.y != found->second.offset.y);
  bool resizes = !(pos.flags & SWP_NOSIZE) && (pos.cx != rect.right - rect.left || pos.cy != rect.bottom - rect.top);
  if (!moves && !resizes) return;
  LogRefusal(pos, rect, origin, found->second.offset);
  if (moves) pos.flags |= SWP_NOMOVE;
  if (resizes) pos.flags |= SWP_NOSIZE;
  PassToOwner(page, found->second, pos, rect, origin, moves, resizes);
}

inline bool RestoringItself(HWND page) noexcept {
  auto found = Docked().find(page);
  return found != Docked().end() && found->second.restoringItself;
}

inline void HandStateToOwner(HWND page, bool maximized) noexcept {
  auto found = Docked().find(page);
  HWND owner = GetWindow(page, GW_OWNER);
  if (found == Docked().end()) return;
  found->second.statePending = false;
  if (!owner || !(IsZoomed(page) || IsIconic(page))) return;
  WINDOWPLACEMENT placement{};
  placement.length = sizeof(placement);
  if (GetWindowPlacement(page, &placement)) {
    placement.showCmd = SW_SHOWNOACTIVATE;
    found->second.restoringItself = true;
    SetWindowPlacement(page, &placement);
    found = Docked().find(page);
    if (found != Docked().end()) found->second.restoringItself = false;
  }
  LaunchLog::Write(L"window", maximized ? L"page window was maximized from outside: maximizing its app window instead" : L"page window was minimized from outside: minimizing its app window instead");
  ShowWindow(owner, maximized ? SW_MAXIMIZE : SW_MINIMIZE);
}

inline void HandStateToOwnerOnce(HWND page) noexcept {
  auto found = Docked().find(page);
  if (found == Docked().end() || found->second.statePending) return;
  found->second.statePending = PostMessageW(page, HandStateToOwnerMessage, IsZoomed(page) ? 1 : 0, 0) != FALSE;
}

inline LRESULT CALLBACK DockProcedure(HWND page, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) noexcept {
  if (message == MoveOwnerMessage) {
    MoveOwner(page);
    return 0;
  }
  if (message == HandStateToOwnerMessage) {
    HandStateToOwner(page, wParam != 0);
    return 0;
  }
  bool takenOver = (message == WM_WINDOWPOSCHANGING || message == WM_WINDOWPOSCHANGED) && lParam && (IsZoomed(page) || IsIconic(page)) && EngineCallDepth() == 0 && !RestoringItself(page);
  if (message == WM_WINDOWPOSCHANGING && lParam) {
    if (takenOver) reinterpret_cast<WINDOWPOS *>(lParam)->flags |= SWP_NOMOVE | SWP_NOSIZE;
    else if (!RestoringItself(page)) HoldInPlace(page, *reinterpret_cast<WINDOWPOS *>(lParam));
  }
  LRESULT result = DefSubclassProc(page, message, wParam, lParam);
  if (message == WM_WINDOWPOSCHANGED && takenOver) HandStateToOwnerOnce(page);
  if (message == WM_WINDOWPOSCHANGED) Remember(page);
  if (message == WM_NCDESTROY) {
    Docked().erase(page);
    RemoveWindowSubclass(page, &DockProcedure, id);
  }
  return result;
}

inline void Dock(HWND page) noexcept {
  if (Docked().contains(page)) return;
  if (!SetWindowSubclass(page, &DockProcedure, DockSubclassId, 0)) return;
  Docked().emplace(page, DockedPage{});
  Remember(page);
}

inline void CALLBACK PageShown(HWINEVENTHOOK, DWORD, HWND hwnd, LONG object, LONG child, DWORD, DWORD) noexcept {
  if (object != OBJID_WINDOW || child != CHILDID_SELF || !hwnd) return;
  HWND owner = GetWindow(hwnd, GW_OWNER);
  if (!owner || !IsPageClass(hwnd) || !IsAppWindow() || !IsAppWindow()(owner)) return;
  Dock(hwnd);
}

inline void Install(std::function<bool(HWND)> isAppWindow) noexcept {
  static bool installed = false;
  if (installed) return;
  IsAppWindow() = std::move(isAppWindow);
  HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, &PageShown, GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
  installed = hook != nullptr;
  LaunchLog::Write(L"window", installed ? L"page windows dock to their app window" : L"page windows not docked: SetWinEventHook failed");
}

}
