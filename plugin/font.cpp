#include "font.h"
#include "gta_string.h"
#include "plugin.h"

static const float fChsWidth = 32.0f;
static const float fChsWidthFix = -1.0f;
static const float fSpriteWidth = 64.0f;
static const float fSpriteHeight = 66.0f;
static const float fTextureResolution = 4096.0f;
static const float fTextureRowsCount = fTextureResolution / fSpriteHeight;
static const float fTextureColumnsCount = fTextureResolution / fSpriteWidth;
static const float fRatio = 4.0f;

void *CNFont;

void *__fastcall CFont::LoadTextureCB(void *pDictionary, int, uint hash)
{
    auto result = plugin.game.Dictionary_grcTexturePC_GetElementByKey(pDictionary, hash);

    CNFont = plugin.game.Dictionary_grcTexturePC_GetElementByKey(pDictionary,
                                                                 plugin.game.Hash_HashStringFromSeediCase("font_chs"));

    return result;
}

const GTAChar *CFont::SkipWord_Prolog(std::uintptr_t address)
{
    auto ptr = reinterpret_cast<const GTAChar *>(address & 0x7FFFFFFF);

    if ((address & 0x80000000) == 0)
    {
        ptr = SkipWord(ptr);
    }

    SkipSpecialPunctuationMarks(ptr);

    return ptr;
}

bool CFont::IsSpecialPunctuationMark(GTAChar chr)
{
#if 0
    return false;
#else
    // —、。《》「」『』（）！，－：；？～
    return
        // chr == L'—' ||
        chr == L'、' || chr == L'。' ||
        // chr == L'《' ||
        chr == L'》' ||
        // chr == L'「' ||
        chr == L'」' ||
        // chr == L'（' ||
        chr == L'）' ||
        // chr == L'『' ||
        chr == L'』' || chr == L'！' || chr == L'，' ||
        // chr == L'－' ||
        chr == L'：' || chr == L'；' || chr == L'？' || chr == L'～' || chr == L'…';
#endif
}

void CFont::SkipSpecialPunctuationMarks(const GTAChar *&str)
{
    while (IsSpecialPunctuationMark(*str))
    {
        ++str;
    }
}

void CFont::AddSpecialPunctuationMarksWidth(const GTAChar *&str, float *width)
{
    while (IsSpecialPunctuationMark(*str))
    {
        *width += GetCharacterSizeNormalDispatch(*str - 0x20);
        ++str;
    }
}

const GTAChar *CFont::SkipWord(const GTAChar *str)
{
    if (str == nullptr)
    {
        return str;
    }

    auto begin = str;
    auto current = str;

    while (true)
    {
        GTAChar chr = *current;

        if (chr == ' ' || chr == '~' || chr == 0)
        {
            break;
        }

        if (!IsNativeChar(chr))
        {
            if (current == begin)
            {
                ++current;
            }

            break;
        }
        else
        {
            ++current;
        }
    }

    SkipSpecialPunctuationMarks(current);

    return current;
}

const GTAChar *CFont::SkipSpaces(const GTAChar *text)
{
    if (text == nullptr)
        return text;

    while (*text == ' ')
    {
        ++text;
    }

    return text;
}

float CFont::GetMaxWordWidth(const GTAChar *text)
{
    if (text == nullptr)
        return 0.0f;

    float max_word_width = 0.0f;

    while (*text != 0)
    {
        float word_width = GetStringWidthRemake(text, false);

        if (word_width > max_word_width)
            max_word_width = word_width;

        text = SkipSpaces(SkipWord(text));
    }

    return max_word_width;
}

static void *s_fnGetStringWidthOriginal = nullptr;

static bool HasButtonLikeToken(const GTAChar *str)
{
    if (str == nullptr)
    {
        return false;
    }
    for (auto p = str; *p != 0; ++p)
    {
        if (*p != '~')
        {
            continue;
        }
        const auto *t = p + 1;
        // 按键/输入类 token 才需要原版测宽（方向键图标等）
        if ((t[0] == 'P' && t[1] == 'A' && t[2] == 'D' && t[3] == '_') ||
            (t[0] == 'A' && t[1] == 'C' && t[2] == 'C' && t[3] == 'E' && t[4] == 'P' && t[5] == 'T') ||
            (t[0] == 'C' && t[1] == 'A' && t[2] == 'N' && t[3] == 'C' && t[4] == 'E' && t[5] == 'L') ||
            (t[0] == 'I' && t[1] == 'N' && t[2] == 'P' && t[3] == 'U' && t[4] == 'T' && t[5] == '_'))
        {
            return true;
        }
        while (*p != 0 && *p != '~')
        {
            ++p;
        }
        if (*p == 0)
        {
            break;
        }
    }
    return false;
}

void CFont::InitGetStringWidthTrampoline()
{
    auto start = static_cast<uint8_t *>(injector::aslr_ptr(0x88A690).get());
    auto cont = static_cast<uint8_t *>(injector::aslr_ptr(0x88A695).get());

    auto tramp =
        static_cast<uint8_t *>(::VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (tramp == nullptr)
    {
        return;
    }

    // 原函数入口为 5 字节 mov eax, imm32，MakeJMP 会覆盖这 5 字节
    tramp[0] = start[0];
    tramp[1] = start[1];
    tramp[2] = start[2];
    tramp[3] = start[3];
    tramp[4] = start[4];
    tramp[5] = 0xE9;
    const auto rel = static_cast<int32_t>(cont - (tramp + 10));
    std::memcpy(tramp + 6, &rel, sizeof(rel));

    s_fnGetStringWidthOriginal = tramp;
}

float CFont::GetStringWidthRemake(const GTAChar *str, bool get_all)
{
    // 仅按键/图标 token 走原版测宽（ESC 底栏方向键）。
    // ~n~ 换行、~g~ 颜色等仍走 remake，否则退出确认框等中文会乱行。
    if (str != nullptr && HasButtonLikeToken(str) && s_fnGetStringWidthOriginal != nullptr)
    {
        return injector::cstd<float(const GTAChar *, bool)>::call(s_fnGetStringWidthOriginal, str, get_all);
    }

    float current_width = 0.0f, max_width = 0.0f;
    bool had_word = false;
    auto render_index = plugin.game.Font_GetRenderIndex();
    auto &using_details = plugin.game.game_addr.pFont_Details[render_index];

    TokenStruct token_data;
    GTAChar token_string[64];

    if (str == nullptr)
    {
        return 0.0f;
    }

    while (true)
    {
        auto chr = *str;

        if (chr == 0)
        {
            break;
        }
        else if (chr == ' ')
        {
            if (!get_all)
                break;
        }
        else if (!IsNativeChar(chr))
        {
            // 汉字
            // 有可能是英语单词+汉字，要断开
            if (had_word && !get_all)
            {
                break;
            }
        }
        else if (chr == '~')
        {
            // token
            if (had_word && !get_all)
                break;

            // 91BEC3
            token_string[0] = 0;

            int token_type = plugin.game.Font_ParseToken(++str, token_string, &token_data);

            if (token_data.f110 == 0)
            {
                // 91C0B4
                if (token_type < 256 || token_type > 300)
                {
                    // 91C116
                    if (gta_string::gtaWcslen(token_string) > 0)
                    {
                        // 91C122
                        plugin.game.Font_AddTokenStringWidth(token_string, &current_width, render_index);
                        had_word = true;
                    }
                }
                else
                {
                    // 91C0C3
                    current_width +=
                        plugin.game.game_addr.pFont_ButtonWidths[token_type - 255] * using_details.fBlipScaleX;
                    had_word = true;
                }
            }
            else
            {
                // 91BF11
                for (int token_index = 0; token_index < 4; ++token_index)
                {
                    switch (token_data.a110[token_index])
                    {
                    case 1: {
                        // 91BF26
                        current_width += plugin.game.game_addr.pFont_ButtonWidths[token_data.f0[token_index] - 255] *
                                         using_details.fBlipScaleX;
                        had_word = true;

                        break;
                    }

                    case 2: {
                        // 91BF53
                        plugin.game.Font_AddTokenStringWidth(token_data.f10[token_index], &current_width, render_index);
                        had_word = true;
                        break;
                    }

                    default: {
                        break;
                    }
                    }
                }
            }

            // 91BF9E
            if (token_type >= 1000)
            {
                // 91BFA6
                current_width += *plugin.game.game_addr.pFont_BlipWidth * using_details.fBlipScaleX;
                had_word = true;
            }

            // 91BFD3
            while (*str != '~')
            {
                // 91BFE0
                if (*str == 'n')
                {
                    max_width = std::max(max_width, current_width);
                    current_width = 0.0f;
                }

                ++str;
            }

            // 91C013
            // 跳过后边的'~'
            ++str;

            AddSpecialPunctuationMarksWidth(str, &current_width);

            // 处理token后立即判断分词，配合ProcessString逻辑
            if (!get_all)
            {
                break;
            }

            continue;
        }

        // 累加字符宽度
        current_width += GetCharacterSizeNormalDispatch(chr - 0x20);
        had_word = true;
        ++str;

        auto old_ptr = str;
        AddSpecialPunctuationMarksWidth(str, &current_width);

        // 解决标英交替时的宽度计算错误
        if (str != old_ptr && !get_all)
        {
            break;
        }

        // 计算汉字宽度之后立即判断一次分词
        if (!IsNativeChar(chr) && !get_all)
        {
            break;
        }
    }

    return std::max(current_width, max_width);
}

float CFont::GetStringWidthGetAllDetour(const GTAChar *str, bool get_all)
{
    return GetStringWidthRemake(str, true);
}

void CFont::ProcessStringRoutine(float x, float y, const GTAChar *str, void *a4)
{
    plugin.game.Font_ProcessString(x, y, plugin.string_table.GetString(str), a4);
}

float CFont::GetCHSCharacterSizeNormal()
{
    auto render_index = plugin.game.Font_GetRenderIndex();
    auto &using_details = plugin.game.game_addr.pFont_Details[render_index];
    auto &using_font_data = plugin.game.game_addr.pFont_Datas[using_details.nFont];

    float extra_width = using_font_data.fWidthOfSpaceBetweenChars[using_details.nExtraWidthIndex];

    return (((fChsWidth + fChsWidthFix + extra_width) / *plugin.game.game_addr.pFont_ResolutionX +
             using_details.fEdgeSize2) *
            using_details.fScaleX);
}

float CFont::GetCharacterSizeNormalDispatch(GTAChar chr)
{
    if (IsNativeChar(chr + 0x20))
    {
        return plugin.game.Font_GetCharacterSizeNormal(chr);
    }
    else
    {
        return GetCHSCharacterSizeNormal();
    }
}

float CFont::GetCHSCharacterSizeDrawing(bool use_extra_width)
{
    float extra_width = 0.0f;

    auto render_state = plugin.game.game_addr.pFont_RenderState;
    auto &using_font_data = plugin.game.game_addr.pFont_Datas[render_state->nFont];

    if (use_extra_width)
    {
        extra_width = using_font_data.fWidthOfSpaceBetweenChars[render_state->nExtraWidthIndex];
    }

    return ((fChsWidth + fChsWidthFix + extra_width) / *plugin.game.game_addr.pFont_ResolutionX +
            render_state->fEdgeSize) *
           render_state->fScaleX;
}

float CFont::GetCharacterSizeDrawingDispatch(GTAChar chr, bool use_extra_width)
{
    if (IsNativeChar(chr + 0x20))
    {
        return plugin.game.Font_GetCharacterSizeDrawing(chr, use_extra_width);
    }
    else
    {
        return GetCHSCharacterSizeDrawing(use_extra_width);
    }
}

void CFont::PrintCHSChar(float x, float y, GTAChar chr)
{
    auto [row, column] = plugin.char_table.GetCharPos(chr);
    auto render_state = plugin.game.game_addr.pFont_RenderState;

    // 原版游戏实际截取的图块在64*80格子中的坐标是
    //  x 0.0
    //  y 4.4
    //  w 64.0
    //  h 78.4912

    // 通过尝试得出的relative_char_rect在64*78.4912矩形中的偏移
    float x_delta = 0.0f;
    float y_delta = 5.0f;

    // 先计算字符图片在64*78.4912矩形内的位置
    CRect relative_char_rect;
    relative_char_rect.bottom_left.x = x_delta;
    relative_char_rect.top_right.x = relative_char_rect.bottom_left.x + fSpriteWidth;
    relative_char_rect.top_right.y = y_delta;
    relative_char_rect.bottom_left.y = relative_char_rect.top_right.y + fSpriteHeight;

    // 截取纹理的位置
    CRect texture_rect;
    texture_rect.bottom_left.x = static_cast<float>(column) * fSpriteWidth / fTextureResolution;
    texture_rect.bottom_left.y = static_cast<float>(row + 1) * fSpriteHeight / fTextureResolution;
    texture_rect.top_right.x = static_cast<float>(column + 1) * fSpriteWidth / fTextureResolution;
    texture_rect.top_right.y = static_cast<float>(row) * fSpriteHeight / fTextureResolution;

    float old_screen_character_width =
        (fChsWidth / *plugin.game.game_addr.pFont_ResolutionX + render_state->fEdgeSize) * render_state->fScaleX;

    float old_screen_character_height = render_state->fScaleY * 0.06558f;

    // 64*78.4912图块在屏幕上的位置
    CRect old_screen_rect;
    old_screen_rect.bottom_left.x = x;
    old_screen_rect.bottom_left.y = y + old_screen_character_height;
    old_screen_rect.top_right.x = x + old_screen_character_width;
    old_screen_rect.top_right.y = y;

    auto flt_proj = [](float old_lb, float old_ub, float old_val, float new_lb, float new_ub) {
        auto old_range = old_ub - old_lb;
        auto old_diff = old_val - old_lb;
        auto new_range = new_ub - new_lb;

        return old_diff / old_range * new_range + new_lb;
    };

    // 将virtual_char_rect投影到old_screen_rect中，得到real_screen_rect
    CRect real_screen_rect;

    // 计算y的第二个参数越小，字的y长度越大
    real_screen_rect.top_right.x = flt_proj(0.0f, 64.0f, relative_char_rect.top_right.x, old_screen_rect.bottom_left.x,
                                            old_screen_rect.top_right.x);

    real_screen_rect.top_right.y = flt_proj(0.0f, 74.4912f, relative_char_rect.top_right.y, old_screen_rect.top_right.y,
                                            old_screen_rect.bottom_left.y);

    real_screen_rect.bottom_left.x = flt_proj(0.0f, 64.0f, relative_char_rect.bottom_left.x,
                                              old_screen_rect.bottom_left.x, old_screen_rect.top_right.x);

    real_screen_rect.bottom_left.y = flt_proj(0.0f, 74.4912f, relative_char_rect.bottom_left.y,
                                              old_screen_rect.top_right.y, old_screen_rect.bottom_left.y);

    // 地图等 UI 可能使用非 0/1/3 的 nFont，必须始终切到中文字库
    if (CNFont != nullptr)
    {
        plugin.game.Graphics_SetRenderState(CNFont);
    }

    plugin.game.Font_Render2DPrimitive(&real_screen_rect, &texture_rect, render_state->field_18, false);
}

void CFont::PrintCharDispatch(float x, float y, GTAChar chr, bool buffered)
{
    if (plugin.game.game_addr.pFont_RenderState->TokenType != 0 || IsNativeChar(chr + 0x20) || IsNativeChar(chr))
    {
        plugin.game.Font_PrintChar(x, y, chr, buffered);
    }
    else
    {
        // 常规菜单：PrintChar(code) 查 code+0x20
        // ESC 地图区域名：实际汉字 = code+0x40
        // 两者都在字库时：地图 HUD（右上/顶部）用 +0x40，否则 +0x20
        const auto shifted20 = static_cast<GTAChar>(chr + 0x20);
        const auto shifted40 = static_cast<GTAChar>(chr + 0x40);
        const bool has20 = plugin.char_table.Has(shifted20);
        const bool has40 = plugin.char_table.Has(shifted40);

        GTAChar lookup = shifted20;
        if (has20 && has40)
        {
            const bool map_pos = (x > 0.45f && (y < 0.28f || y > 0.78f));
            lookup = map_pos ? shifted40 : shifted20;
        }
        else if (has40 && !has20)
        {
            lookup = shifted40;
        }
        else if (!has20 && !has40)
        {
            lookup = plugin.char_table.Has(chr) ? chr : shifted20;
        }

        if (lookup == 0x3000)
        {
            return;
        }

        if (y < -0.06558f || y > 1.0f)
        {
            return;
        }

        if (-(GetCHSCharacterSizeDrawing(true) / plugin.game.game_addr.pFont_RenderState->fScaleX) > x || x > 1.0f)
        {
            return;
        }

        PrintCHSChar(x, y, lookup);
    }
}
