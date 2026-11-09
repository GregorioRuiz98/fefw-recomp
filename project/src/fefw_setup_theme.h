// SPDX-License-Identifier: GPL-3.0-or-later
// Native settings palette inspired by Goldoire's launcher/theme.css (GPL-3.0-or-later).
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <string>

namespace fefw::setup::ui {
inline COLORREF bg = RGB(20,21,42), surface = RGB(28,29,56), inset = RGB(15,16,32), border = RGB(51,53,90);
inline COLORREF fg = RGB(230,235,243), muted = RGB(152,163,182), accent = RGB(64,204,186), on_accent = RGB(11,26,20);
inline bool contrast = false;
inline HBRUSH background{}, field{}, sidebar{};
inline void init() {
    HIGHCONTRASTW hc{sizeof hc}; SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0);
    contrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    if (contrast) {
        bg = inset = GetSysColor(COLOR_WINDOW); surface = GetSysColor(COLOR_BTNFACE);
        fg = GetSysColor(COLOR_WINDOWTEXT); muted = GetSysColor(COLOR_GRAYTEXT); border = fg;
        accent = GetSysColor(COLOR_HIGHLIGHT); on_accent = GetSysColor(COLOR_HIGHLIGHTTEXT);
    }
    background = CreateSolidBrush(bg); field = CreateSolidBrush(surface); sidebar = CreateSolidBrush(inset);
}
inline void rect(HDC dc, RECT r, COLORREF fill, COLORREF line, int radius = 0) {
    HBRUSH b = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, line);
    auto ob = SelectObject(dc,b); auto op = SelectObject(dc,pen);
    if (radius) RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius); else Rectangle(dc,r.left,r.top,r.right,r.bottom);
    SelectObject(dc,op); SelectObject(dc,ob); DeleteObject(pen); DeleteObject(b);
}
inline void label(HDC dc, const wchar_t* text, RECT r, COLORREF color, HFONT font, UINT format) {
    auto old = SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    DrawTextW(dc,text,-1,&r,format); SelectObject(dc,old);
}
inline void button(const DRAWITEMSTRUCT& d, HFONT font, bool primary, bool navigation, bool selected) {
    const bool disabled = !IsWindowEnabled(d.hwndItem), pressed = (d.itemState & ODS_SELECTED) != 0;
    const bool hot = GetPropW(d.hwndItem,L"FEFW_Hover") != nullptr;
    COLORREF fill = navigation ? (selected || hot ? surface : inset) : primary && !disabled ? (pressed ? RGB(47,174,158) : accent) : pressed || hot ? border : surface;
    COLORREF text = disabled ? muted : primary && !navigation ? on_accent : selected && navigation ? accent : fg;
    rect(d.hDC,d.rcItem,fill,navigation ? fill : pressed || hot ? accent : border,10);
    if (selected && navigation) { RECT edge=d.rcItem; edge.right=edge.left+3; rect(d.hDC,edge,accent,accent); }
    wchar_t caption[256]{}; GetWindowTextW(d.hwndItem,caption,256);
    RECT r=d.rcItem; r.left+=navigation?18:8; r.right-=8;
    label(d.hDC,caption,r,text,font,DT_VCENTER|DT_SINGLELINE|(navigation?DT_LEFT|DT_NOPREFIX:DT_CENTER)|DT_END_ELLIPSIS|((d.itemState&ODS_NOACCEL)?DT_HIDEPREFIX:0));
    if (d.itemState & ODS_FOCUS) { InflateRect(&r,-3,-3); SetTextColor(d.hDC,accent); DrawFocusRect(d.hDC,&r); }
}
inline LRESULT CALLBACK combo_frame(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    const LRESULT result=DefSubclassProc(h,msg,w,l);
    if(!contrast&&(msg==WM_PAINT||msg==WM_PRINTCLIENT||msg==WM_PRINT)) {
        HDC dc=msg==WM_PAINT?GetDC(h):reinterpret_cast<HDC>(w);
        if(dc) {
            COMBOBOXINFO c{sizeof c}; GetComboBoxInfo(h,&c); RECT r{}; GetClientRect(h,&r);
            rect(dc,c.rcButton,surface,surface);
            const int x=(c.rcButton.left+c.rcButton.right)/2,y=(c.rcButton.top+c.rcButton.bottom)/2;
            POINT arrow[]={{x-4,y-2},{x+4,y-2},{x,y+2}};
            HBRUSH b=CreateSolidBrush(IsWindowEnabled(h)?fg:muted); auto ob=SelectObject(dc,b); auto op=SelectObject(dc,GetStockObject(NULL_PEN));
            Polygon(dc,arrow,3); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(b);
            HPEN pen=CreatePen(PS_SOLID,1,GetFocus()==h?accent:border); op=SelectObject(dc,pen); ob=SelectObject(dc,GetStockObject(NULL_BRUSH));
            Rectangle(dc,r.left,r.top,r.right,r.bottom); SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(pen);
            if(msg==WM_PAINT) ReleaseDC(h,dc);
        }
    }
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(h,combo_frame,2);
    return result;
}
inline LRESULT CALLBACK interactive(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR, DWORD_PTR) {
    if (msg==WM_MOUSEMOVE && !GetPropW(h,L"FEFW_Hover")) {
        SetPropW(h,L"FEFW_Hover",reinterpret_cast<HANDLE>(1)); TRACKMOUSEEVENT t{sizeof t,TME_LEAVE,h,0}; TrackMouseEvent(&t); InvalidateRect(h,nullptr,FALSE);
    } else if (msg==WM_MOUSELEAVE) { RemovePropW(h,L"FEFW_Hover"); InvalidateRect(h,nullptr,FALSE); }
    else if (msg==WM_NCDESTROY) { RemovePropW(h,L"FEFW_Hover"); RemoveWindowSubclass(h,interactive,1); }
    return DefSubclassProc(h,msg,w,l);
}
inline void combo(const DRAWITEMSTRUCT& d, HFONT font) {
    const bool selected = (d.itemState & ODS_SELECTED) != 0 && !(d.itemState & ODS_COMBOBOXEDIT);
    rect(d.hDC,d.rcItem,selected?border:surface,selected?border:surface);
    if (d.itemID == static_cast<UINT>(-1)) return;
    const int size = static_cast<int>(SendMessageW(d.hwndItem,CB_GETLBTEXTLEN,d.itemID,0));
    if (size < 0 || size > 8192) return;
    std::wstring caption(size+1,L'\0'); SendMessageW(d.hwndItem,CB_GETLBTEXT,d.itemID,reinterpret_cast<LPARAM>(caption.data()));
    RECT r=d.rcItem; r.left+=10; r.right-=6;
    label(d.hDC,caption.c_str(),r,IsWindowEnabled(d.hwndItem)?fg:muted,font,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    if (d.itemState & ODS_FOCUS) DrawFocusRect(d.hDC,&r);
}
inline LRESULT colors(UINT msg,WPARAM dc,LPARAM child,HFONT small_font) {
    auto hdc=reinterpret_cast<HDC>(dc); auto h=reinterpret_cast<HWND>(child);
    SetTextColor(hdc,IsWindowEnabled(h)?(reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0))==small_font?muted:fg):muted);
    const bool input=msg==WM_CTLCOLOREDIT || msg==WM_CTLCOLORLISTBOX;
    SetBkColor(hdc,input?surface:bg); return reinterpret_cast<LRESULT>(input?field:background);
}
}
