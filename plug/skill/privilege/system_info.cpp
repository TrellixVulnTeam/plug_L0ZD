#include "system_info.h"

#include "../../../base_plug/include/regedit.h"

namespace plug
{
const wchar_t* regedit_sub_key = L"SOFTWARE\\LinkPlug";
const wchar_t* regedit_key_name = L"Version";


bool CreateRegedit()
{
    RegKey key(GetHKEY(CURRENT_USER), regedit_sub_key, KEY_SET_VALUE);
    if (key.Valid())
    {
        key.WriteValue(regedit_key_name, L"");
    }

    return true;
}

bool DeleteRegedit()
{
    return true;
}

bool ReadValue()
{
    return true;
}

bool WriteValue()
{
    return true;
}

HKEY GetHKEY(Regedit regedit)
{
    HKEY hKey = NULL;
    switch (regedit)
    {
    case Regedit::ROOT:
        hKey = HKEY_CLASSES_ROOT;
        break;
    case Regedit::CURRENT_USER:
        hKey = HKEY_CURRENT_USER;
        break;
    case Regedit::LOCAL:
        hKey = HKEY_LOCAL_MACHINE;
        break;
    case Regedit::USER:
        hKey = HKEY_USERS;
        break;
    case Regedit::CONFIG:
        hKey = HKEY_CURRENT_CONFIG;
        break;
    default:
        break;
    }

    return hKey;
}

}
