#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <ole2.h>
 #include <uiautomation.h>
#endif

#include "Announcer.h"

namespace astralay::ui
{

namespace
{
    void fallbackAnnounce (const juce::String& text, bool interrupt)
    {
        juce::AccessibilityHandler::postAnnouncement (text, interrupt ? juce::AccessibilityHandler::AnnouncementPriority::high
                                                                       : juce::AccessibilityHandler::AnnouncementPriority::medium);
    }
}

void announceFrom (juce::Component& source, const juce::String& text)
{
    for (auto* c = &source; c != nullptr; c = c->getParentComponent())
    {
        if (auto* target = dynamic_cast<AnnouncementTarget*> (c))
        {
            target->announce (text);
            return;
        }
    }

    fallbackAnnounce (text, true);
}

#if JUCE_WINDOWS

namespace
{
    // Values from the Windows 10 1709 SDK, declared here so older SDK targets still compile.
    constexpr int notificationKindActionCompleted = 2;
    constexpr int notificationProcessingImportantMostRecent = 1;
    constexpr int notificationProcessingAll = 2;

    /** uiautomationcore.dll entry points, loaded at run time since UiaRaiseNotificationEvent only
        exists on Windows 10 1709 and later.
    */
    struct UiaFunctions
    {
        using RaiseNotification = HRESULT (WINAPI*) (IRawElementProviderSimple*, int, int, BSTR, BSTR);
        using HostProviderFromHwnd = HRESULT (WINAPI*) (HWND, IRawElementProviderSimple**);
        using ReturnRawElementProvider = LRESULT (WINAPI*) (HWND, WPARAM, LPARAM, IRawElementProviderSimple*);
        using DisconnectProvider = HRESULT (WINAPI*) (IRawElementProviderSimple*);
        using ClientsAreListening = BOOL (WINAPI*) ();

        RaiseNotification raiseNotification = nullptr;
        HostProviderFromHwnd hostProviderFromHwnd = nullptr;
        ReturnRawElementProvider returnRawElementProvider = nullptr;
        DisconnectProvider disconnectProvider = nullptr;
        ClientsAreListening clientsAreListening = nullptr;

        bool isAvailable() const noexcept
        {
            return raiseNotification != nullptr && hostProviderFromHwnd != nullptr && returnRawElementProvider != nullptr;
        }

        static const UiaFunctions& get()
        {
            static const UiaFunctions functions = []
            {
                UiaFunctions f;

                if (auto* module = LoadLibraryW (L"uiautomationcore.dll"))
                {
                    const auto load = [module] (auto& fn, const char* name)
                    {
                        fn = reinterpret_cast<std::remove_reference_t<decltype (fn)>> (reinterpret_cast<void*> (GetProcAddress (module, name)));
                    };

                    load (f.raiseNotification, "UiaRaiseNotificationEvent");
                    load (f.hostProviderFromHwnd, "UiaHostProviderFromHwnd");
                    load (f.returnRawElementProvider, "UiaReturnRawElementProvider");
                    load (f.disconnectProvider, "UiaDisconnectProvider");
                    load (f.clientsAreListening, "UiaClientsAreListening");
                }

                return f;
            }();

            return functions;
        }
    };

    /** A minimal UIA element for the hidden window. It is neither a control nor content, so screen
        readers never land on it; it exists only to be the source of notification events.
    */
    class AnnouncementProvider final : public IRawElementProviderSimple
    {
    public:
        explicit AnnouncementProvider (HWND windowToUse) : window (windowToUse) {}

        HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** result) override
        {
            if (result == nullptr)
                return E_POINTER;

            if (riid == __uuidof (IUnknown) || riid == __uuidof (IRawElementProviderSimple))
            {
                *result = static_cast<IRawElementProviderSimple*> (this);
                AddRef();
                return S_OK;
            }

            *result = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override   { return (ULONG) InterlockedIncrement (&references); }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const auto remaining = InterlockedDecrement (&references);

            if (remaining == 0)
                delete this;

            return (ULONG) remaining;
        }

        HRESULT STDMETHODCALLTYPE get_ProviderOptions (ProviderOptions* result) override
        {
            if (result == nullptr)
                return E_POINTER;

            *result = (ProviderOptions) (ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetPatternProvider (PATTERNID, IUnknown** result) override
        {
            if (result == nullptr)
                return E_POINTER;

            *result = nullptr;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetPropertyValue (PROPERTYID property, VARIANT* result) override
        {
            if (result == nullptr)
                return E_POINTER;

            VariantInit (result);

            switch (property)
            {
                case UIA_ControlTypePropertyId:
                    result->vt = VT_I4;
                    result->lVal = UIA_CustomControlTypeId;
                    break;

                case UIA_IsControlElementPropertyId:
                case UIA_IsContentElementPropertyId:
                case UIA_IsKeyboardFocusablePropertyId:
                    result->vt = VT_BOOL;
                    result->boolVal = VARIANT_FALSE;
                    break;

                default:
                    break;
            }

            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE get_HostRawElementProvider (IRawElementProviderSimple** result) override
        {
            if (result == nullptr)
                return E_POINTER;

            return UiaFunctions::get().hostProviderFromHwnd (window, result);
        }

    private:
        ~AnnouncementProvider() = default;

        HWND window;
        LONG references = 1;
    };

    constexpr auto windowClassName = L"AstralayScreenReaderAnnouncer";

    LRESULT CALLBACK announcerWindowProc (HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_GETOBJECT && static_cast<long> (lParam) == static_cast<long> (UiaRootObjectId))
            if (auto* provider = reinterpret_cast<IRawElementProviderSimple*> (GetWindowLongPtrW (window, GWLP_USERDATA)))
                return UiaFunctions::get().returnRawElementProvider (window, wParam, lParam, provider);

        return DefWindowProcW (window, message, wParam, lParam);
    }

    HINSTANCE moduleInstance()
    {
        return static_cast<HINSTANCE> (juce::Process::getCurrentModuleInstanceHandle());
    }

    bool registerWindowClass()
    {
        static const bool registered = []
        {
            WNDCLASSEXW windowClass {};
            windowClass.cbSize = sizeof (windowClass);
            windowClass.lpfnWndProc = announcerWindowProc;
            windowClass.hInstance = moduleInstance();
            windowClass.lpszClassName = windowClassName;

            return RegisterClassExW (&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        }();

        return registered;
    }
}

struct Announcer::Impl
{
    explicit Impl (juce::Component& windowToUse) : owner (windowToUse) {}

    ~Impl()
    {
        destroyWindow();
    }

    bool announce (const juce::String& text, bool interrupt)
    {
        const auto& uia = UiaFunctions::get();

        if (! uia.isAvailable() || ! ensureWindow())
            return false;

        if (uia.clientsAreListening != nullptr && ! uia.clientsAreListening())
            return false;

        auto* displayString = SysAllocString (text.toWideCharPointer());
        auto* activityId = SysAllocString (L"Astralay");

        const auto result = uia.raiseNotification (provider, notificationKindActionCompleted,
                                                   interrupt ? notificationProcessingImportantMostRecent : notificationProcessingAll,
                                                   displayString, activityId);

        SysFreeString (displayString);
        SysFreeString (activityId);
        return SUCCEEDED (result);
    }

private:
    /** Creates the hidden window under the editor's native window, re-creating it if the editor
        has moved to a different native window.
    */
    bool ensureWindow()
    {
        auto* parent = static_cast<HWND> (owner.getWindowHandle());

        if (parent == nullptr || ! registerWindowClass())
            return false;

        if (window != nullptr && IsWindow (window) && GetParent (window) == parent)
            return true;

        destroyWindow();

        window = CreateWindowExW (0, windowClassName, L"", WS_CHILD | WS_DISABLED, 0, 0, 1, 1,
                                  parent, nullptr, moduleInstance(), nullptr);

        if (window == nullptr)
            return false;

        provider = new AnnouncementProvider (window);
        SetWindowLongPtrW (window, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (provider));
        return true;
    }

    void destroyWindow()
    {
        if (window != nullptr)
        {
            SetWindowLongPtrW (window, GWLP_USERDATA, 0);

            if (IsWindow (window))
                DestroyWindow (window);

            window = nullptr;
        }

        if (provider != nullptr)
        {
            if (auto disconnect = UiaFunctions::get().disconnectProvider)
                disconnect (provider);

            provider->Release();
            provider = nullptr;
        }
    }

    juce::Component& owner;
    HWND window = nullptr;
    AnnouncementProvider* provider = nullptr;
};

#else

struct Announcer::Impl
{
    explicit Impl (juce::Component&) {}
    bool announce (const juce::String&, bool) { return false; }
};

#endif

Announcer::Announcer (juce::Component& window)
    : impl (std::make_unique<Impl> (window))
{
}

Announcer::~Announcer() = default;

void Announcer::announce (const juce::String& text, bool interrupt)
{
    if (! impl->announce (text, interrupt))
        fallbackAnnounce (text, interrupt);
}

} // namespace astralay::ui
