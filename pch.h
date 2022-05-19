#pragma once

#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <Windows.UI.Xaml.Hosting.DesktopWindowXamlSource.h>
#include <UserConsentVerifierInterop.h>
#include <winstring.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Media.Animation.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Windows.UI.Xaml.Markup.h>
#include <winrt/Windows.UI.Xaml.Documents.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Data.Text.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Credentials.UI.h>
#include <winrt/Windows.Storage.Streams.h>
