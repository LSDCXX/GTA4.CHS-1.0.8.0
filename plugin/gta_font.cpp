#include "gta_font.h"
#include "font.h"
#include "plugin.h"

namespace gta_font
{
void register_patches()
{
    // 搜索"~%c~"找到CFont::ProcessString

    // 储存CFont::ProcessString地址
    plugin.game.game_addr.fnFont_ProcessString = injector::aslr_ptr(0x88B370).get();

    // 劫持菜单项的CFont::ProcessString
    injector::MakeCALL(injector::aslr_ptr(0x88BC11).get(), CFont::ProcessStringRoutine);

    // 不 hook ProcessToken epilog：会影响 ESC 底栏方向键 token 的绘制推进

    // CFont::ProcessString使用了
    // 跳过单词
    injector::MakeJMP(injector::aslr_ptr(0x8859E0).get(), CFont::SkipWord_Prolog);

    // GetStringWidth使用了
    // 获取字符宽度（原版函数内部调用，中文测宽）
    injector::MakeCALL(injector::aslr_ptr(0x884B64).get(), CFont::GetCharacterSizeNormalDispatch);
    injector::MakeCALL(injector::aslr_ptr(0x88A7D5).get(), CFont::GetCharacterSizeNormalDispatch);

    // 查找GetCharacterSizeNormal的引用获得
    injector::MakeCALL(injector::aslr_ptr(0x884D0A).get(), CFont::GetCharacterSizeDrawingDispatch);

    // 查找GetCharacterSizeNormal的引用获得
    injector::MakeCALL(injector::aslr_ptr(0x88A58B).get(), CFont::GetCharacterSizeDrawingDispatch);

    // RenderSingleBuffer使用了
    // 绘制字符
    injector::MakeCALL(injector::aslr_ptr(0x88A4A0).get(), CFont::PrintCharDispatch);

    // 另一个使用PrintChar的函数里面
    injector::MakeCALL(injector::aslr_ptr(0x884D02).get(), CFont::PrintCharDispatch);

    // 加载fonts.wtd中的font_chs
    injector::MakeCALL(injector::aslr_ptr(0x887642).get(), CFont::LoadTextureCB);
    injector::MakeCALL(injector::aslr_ptr(0x887CB6).get(), CFont::LoadTextureCB);

    // 先建原版 GetStringWidth 跳板，再 JMP 到 remake（含 token 的串内部会转调原版）
    CFont::InitGetStringWidthTrampoline();
    injector::MakeJMP(injector::aslr_ptr(0x88A690).get(), CFont::GetStringWidthRemake);

    // 使用了GetStringWidth
    //  GetMaxWordWidth
    injector::MakeJMP(injector::aslr_ptr(0x88B2B0).get(), CFont::GetMaxWordWidth);
}
} // namespace gta_font
