#include "hotkey.h"
#include "music_config.h"

#include <windows.h>
#include <cwctype>
#include <string>

namespace {

std::wstring Trim(const std::wstring& value) {
    size_t start = 0;
    while (start < value.size() && std::iswspace(static_cast<wint_t>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::iswspace(static_cast<wint_t>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::wstring Lower(const std::wstring& value) {
    std::wstring result = value;
    for (auto& c : result) {
        c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
    }
    return result;
}

uint32_t NamedKey(const std::wstring& key) {
    static const struct {
        const wchar_t* name;
        uint32_t vk;
    } kNamed[] = {
        {L"left", VK_LEFT},       {L"right", VK_RIGHT},     {L"up", VK_UP},
        {L"down", VK_DOWN},       {L"home", VK_HOME},       {L"end", VK_END},
        {L"pageup", VK_PRIOR},    {L"page_up", VK_PRIOR},   {L"pgup", VK_PRIOR},
        {L"pagedown", VK_NEXT},   {L"page_down", VK_NEXT},  {L"pgdn", VK_NEXT},
        {L"insert", VK_INSERT},   {L"ins", VK_INSERT},      {L"delete", VK_DELETE},
        {L"del", VK_DELETE},
    };
    for (const auto& named : kNamed) {
        if (key == named.name) {
            return named.vk;
        }
    }
    if (key.size() == 4 && key.compare(0, 3, L"num") == 0 && key[3] >= L'0' && key[3] <= L'9') {
        return static_cast<uint32_t>(VK_NUMPAD0) + static_cast<uint32_t>(key[3] - L'0');
    }
    return 0;
}

}  // namespace

bool ParseHotkey(const wchar_t* str, Hotkey& out) {
    if (!str) {
        return false;
    }
    const std::wstring text = Trim(str);
    if (text.empty()) {
        return false;
    }

    Hotkey parsed;
    size_t pos = 0;
    for (;;) {
        const size_t plus = text.find(L'+', pos);
        if (plus == std::wstring::npos) {
            break;
        }
        const std::wstring token = Lower(Trim(text.substr(pos, plus - pos)));
        if (token.empty()) {
            return false;
        }
        if (token == L"ctrl" || token == L"control") {
            if (parsed.ctrl) return false;
            parsed.ctrl = true;
        } else if (token == L"shift") {
            if (parsed.shift) return false;
            parsed.shift = true;
        } else if (token == L"alt") {
            if (parsed.alt) return false;
            parsed.alt = true;
        } else {
            return false;
        }
        pos = plus + 1;
    }

    const std::wstring key = Lower(Trim(text.substr(pos)));
    if (key.empty()) {
        return false;
    }

    uint32_t vk = 0;
    if (key.size() == 1) {
        const wchar_t c = key[0];
        if (c >= L'a' && c <= L'z') {
            vk = static_cast<uint32_t>(L'A' + (c - L'a'));
        } else if (c >= L'0' && c <= L'9') {
            vk = static_cast<uint32_t>(L'0' + (c - L'0'));
        }
    }
    if (vk == 0) {
        vk = NamedKey(key);
    }
    if (vk == 0) {
        vk = ParseVirtualKey(key.c_str(), 0);
    }
    if (vk == 0) {
        return false;
    }

    parsed.vk = vk;
    out = parsed;
    return true;
}

bool HotkeyMatches(const Hotkey& key, bool ctrlDown, bool shiftDown, bool altDown) {
    return key.vk != 0 && key.ctrl == ctrlDown && key.shift == shiftDown && key.alt == altDown;
}
