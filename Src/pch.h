#pragma once

// 编码核心沿用参照项目：关闭 C++ 异常时 std 容器会报 C4530，属预期（错误路径全走 HRESULT）。
#pragma warning(disable:4530)

#include <windows.h>
#include <atlbase.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::WRL;
